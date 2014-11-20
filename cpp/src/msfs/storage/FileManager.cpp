/*
 * Multimedia Small File Storage System 
 * File Manager Singleton implement to manage file store and download operation
 * author potian@mogujie.com
*/

#include <stdio.h>
#include <string>
#include <iostream>
#include "FileManager.h"
#include "StringUtils.h"
#include "util.h"
#include "Base64.h"
#include "FileLin.h"
#ifdef WIN32
#include <time.h>
#include "pthread.h"

int gettimeofday(struct timeval *tp, void *tzp)
{
	time_t clock;
	struct tm tm;
	SYSTEMTIME wtm;
	GetLocalTime(&wtm);
	tm.tm_year = wtm.wYear - 1900;
	tm.tm_mon = wtm.wMonth - 1;
	tm.tm_mday = wtm.wDay;
	tm.tm_hour = wtm.wHour;
	tm.tm_min = wtm.wMinute;
	tm.tm_sec = wtm.wSecond;
	tm.tm_isdst = -1;
	clock = mktime(&tm);
	tp->tv_sec = clock;
	tp->tv_usec = wtm.wMilliseconds * 1000;
	return (0);
}


LPCWSTR stringToLPCWSTR(std::string orig)
{
	size_t origsize = orig.length() + 1;
	const size_t newsize = 100;
	size_t convertedChars = 0;
	wchar_t *wcstring = (wchar_t *)malloc(sizeof(wchar_t)*(orig.length() - 1));
	mbstowcs_s(&convertedChars, wcstring, origsize, orig.c_str(), _TRUNCATE);
	return wcstring;
}
#else
#include <sys/mount.h>
#include <sys/time.h>
#include <pthread.h>
#endif

using namespace std;

namespace msfs {

int FileManager::initDir() {
		bool isExist = File::isExist(m_disk);
		if (!isExist) {
			u64 ret = File::mkdirNoRecursion(m_disk);
			if (ret) {
				log("The dir[%s] set error for code[%d], \
				    its parent dir may no exists\n", m_disk, ret);
				return -1;
			}
		}
		
		//255 X 255 
		char first[10] = {0};
		char second[10] = {0};
		for (int i = 0; i <= FIRST_DIR_MAX; i++) {
			snprintf(first, 5, "%03d", i);
			string tmp = string(m_disk) + "/" + string(first);
		    int code = File::mkdirNoRecursion(tmp.c_str());
			if (code && (errno != EEXIST)) {
				log("Create dir[%s] error[%d]\n", tmp.c_str(), errno);
				return -1;
			}
			for (int j = 0; j <= SECOND_DIR_MAX; j++) {
				snprintf(second, 5, "%03d", j);
				string tmp2 = tmp + "/" + string(second);
		    	code = File::mkdirNoRecursion(tmp2.c_str());
		    	if (code && (errno != EEXIST)) {
					log("Create dir[%s] error[%d]\n", tmp2.c_str(), errno);
					return -1;
		    	}
				memset(second, 0x0, 10);
			}
			memset(first, 0x0, 10);
		}
		
		return 0;
	}

string FileManager::createFileRelatePath() {
	char first[10] = {0};
	char second[10] = {0};
	m_cs.Enter();
	snprintf(first, 5, "%03d", (int)getFirstDir());
	snprintf(second, 5, "%03d", (int)getSecondDir());
	m_cs.Leave();
	
	struct timeval tv;
	gettimeofday(&tv,NULL);
	u64 usec = tv.tv_sec*1000000 + tv.tv_usec;
#ifndef WIN32
	u64 tid = (u64)pthread_self();
#else
	u64 tid = GetCurrentThreadId();
#endif
	char unique[40];
	snprintf(unique, 30, "%llu_%llu", usec, tid);
	string path = "/" + string(first) + "/" + string(second) 
					+ "/" + string(unique);
	return string(path);
}

int FileManager::uploadFile(const char *type, const void* content, u32 size, 
							char *url, char *ext) {
	//check file size
	if (size > MAX_FILE_SIZE_PER_FILE) {
		log("File size[%d] should less than [%d]\n", size, 
			MAX_FILE_SIZE_PER_FILE);
		return -1;
	}

	//append the type suffix
	string path = createFileRelatePath();
	if (ext)
		path += "_" + string(ext);
	else
		path += "." + string(type);
	
	//construct url with group num
	string groups("g0");
	string fullUrl = groups + path;
	strncpy(url, fullUrl.c_str(), strlen(fullUrl.c_str()));

	//insert map file cache
	m_cs.Enter();
	insertEntry(fullUrl, (u64)size, content);
	updateMapCache();
	m_cs.Leave();
	
	//open and write file then close
	string absPath = string(m_disk) + path;
#ifndef WIN32
	File * tmpFile = new File(absPath.c_str());
	tmpFile->create();
	tmpFile->write(0, size, content);
	delete tmpFile;
	tmpFile = NULL;
#else
	FILE *fp;
	if ((fp = fopen(absPath.c_str(), "wb")) == NULL);
	fwrite(content, size, 1, fp);
	fclose(fp);

#endif

	//increase total file sum
	m_filesCs.Enter();
	m_totFiles++;
	m_filesCs.Leave();
	
	return 0;
}

int FileManager::getRelatePathByUrl(const string &url, string &path) {
	string::size_type pos = url.find("/");
	if (string::npos == pos) {
		log("Url [%s] format illegal.\n",url.c_str());
		return -1;
	}
	path = url.substr(pos);
	return 0;
}

int FileManager::getAbsPathByUrl(const string &url, string &path) {
	string relate;
	if (getRelatePathByUrl(url, relate)) {
		log("Get path from url[%s] error\n", url.c_str());
		return -1;
	}
	path = string(m_disk) + relate;
	return 0;
}

FileManager::Entry* 
FileManager::getOrCreateEntry(const std::string& url, bool create) {
	m_cs.Enter();
	EntryMap::iterator it = m_map.find(url);
	if (it != m_map.end()) {
		log("the map has the file while url:%s\n", url.c_str());
		m_cs.Leave();
		return it->second;
	}
	if (!create) {
		m_cs.Leave();
		return NULL;
	}
	
	string path;
	if (getAbsPathByUrl(url, path)) {
		log("Get abs path from url[%s] error\n", url.c_str());
		m_cs.Leave();
		return NULL;
	}
    	
	Entry *e = new Entry();
#ifndef WIN32
	u64 fileSize = File::getFileSize(path.c_str());
	e->m_fileSize = fileSize;
	e->m_fileContent = new byte[fileSize];
	memset(e->m_fileContent, 0x0, fileSize);
	e->m_lastAccess = time(NULL);
	File* tmpFile = new File(path.c_str());
	tmpFile->open();
	int ret = tmpFile->read(0, fileSize, e->m_fileContent);
	if (ret) {
		log("read file error while url:%s\n", url.c_str());
		delete e;
		e = NULL;
		delete tmpFile;
		tmpFile = NULL;
		m_cs.Leave();
		return NULL;
	}
	delete tmpFile;
	tmpFile = NULL;
#else
	HANDLE pfile;
	pfile = ::CreateFile(stringToLPCWSTR(path), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	u64 filesize = GetFileSize(pfile, NULL);
	e->m_fileSize = filesize;
	e->m_fileContent = new byte[filesize];
	memset(e->m_fileContent, 0x0, filesize);
	e->m_lastAccess = time(NULL);
	DWORD read;
	BOOL res = ReadFile(pfile, e->m_fileContent, filesize, &read, NULL);
	if (!res || read < 0) {
		CloseHandle(pfile);
		delete e;
		e = NULL;
		m_cs.Leave();
		return NULL;
	}
#endif	
	std::pair < map <std::string, Entry*>::iterator, bool> result;
	result = m_map.insert(EntryMap::value_type(url, e));
	if (result.second == false) {
		log("Insert url[%s] to file map error\n", url.c_str());
		delete e;
		e = NULL;
	}
	updateMapCache();
	m_cs.Leave();
	return e;
}

int FileManager::downloadFileByUrl(char *url, void *buf, u32 *size) {
	Entry* en = getOrCreateEntry(url, true);
	if (!en) {
		log("download file error, while url:%s\n", url);
		return -1;
	}
	memcpy(buf, en->m_fileContent, en->m_fileSize);
	*size = (u32)en->m_fileSize;
	en->m_lastAccess = time(NULL);//todo:need prodect with mutex
	return 0;
}

void FileManager::updateMapCache() {
	size_t currSize = m_map.size();
	if (currSize > MAX_FILE_IN_MAP) {
		EntryMap::iterator it = m_map.begin();
		int times = MAX_FILE_IN_MAP - currSize;
		while (it != m_map.end() && times) {
			delete it->second;
			m_map.erase(it++);
			times--;
		}

		it = m_map.begin();
		while (it != m_map.end() && times) {
			time_t currTime = time(NULL);
			if(currTime - it->second->m_lastAccess > 2*60*60) {
				delete it->second;
				m_map.erase(it++);
			}
		}
	}
}

int FileManager::insertEntry(const std::string& url, size_t fileSize, 
							const void* content) {
	if (m_map.size()) {
		EntryMap::iterator it = m_map.find(url);
		if (it != m_map.end()) 
			return -1;
	}
	Entry *e = new Entry();
	e->m_fileSize = fileSize;
	e->m_fileContent = new byte[fileSize];
	e->m_lastAccess = time(NULL);
	memcpy(e->m_fileContent, content, fileSize);
	
	m_cs.Enter();
	pair< map<std::string, Entry*>::iterator, bool> ret;
	ret = m_map.insert(EntryMap::value_type(url, e));
	if (ret.second == false) {
		delete e;
		e = NULL;
	}
	updateMapCache();
	m_cs.Leave();
	return 0;
}

void FileManager::releaseFileCache(const std::string& url) {
	m_cs.Enter();
	const Entry* entry = getEntry(url);
	if (!entry) {
		log("map has not the url::%s\n", url.c_str());
		return;
	}
	delete [] entry->m_fileContent;
	m_map.erase(url);
	m_cs.Leave();
	return;
}

}

