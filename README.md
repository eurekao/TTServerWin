简介：
========

移植TTServer能在Windows环境下编译运行，当前能正常编译运行的包括LoginServer,RouteServer,MsgServer,file_server,msfs。已能正常运行TTserver服务器端所有功能

....

TeamTalk是一套开源的企业办公即时通讯软件，作为整套系统的组成部分之一，TTServer为TeamTalk 客户端提供用户登录，消息转发及存储等基础服务。

TTServer主要包含了以下几种服务器:

- LoginServer (C++): 登录服务器，分配一个负载小的MsgServer给客户端使用
- MsgServer (C++):  消息服务器，提供客户端大部分信令处理功能，包括私人聊天、群组聊天等
- RouteServer (C++):  路由服务器，为登录在不同MsgServer的用户提供消息转发功能
- FileServer (C++): 文件服务器，提供客户端之间得文件传输服务，支持在线以及离线文件传输
- MsfsServer (C++): 图片存储服务器，提供头像，图片传输中的图片存储服务
- DBProxy (JAVA): 数据库代理服务器，提供mysql以及redis的访问服务，屏蔽其他服务器与mysql与redis的直接交互

....


# 运行时准备环境

1.安装java sdk 7

2.设置好jdk中jre的到环境变量PATH中


