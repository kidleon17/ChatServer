#include "chatserver.hpp"
#include "chatservice.hpp"
#include <string>
#include "json.hpp"
#include<functional>
using namespace std;
using namespace placeholders;
using json = nlohmann::json;
ChatServer::ChatServer(EventLoop *loop,const InetAddress& listenAddr,const string nameArg):_server(loop,listenAddr,nameArg),_loop(loop) {
    //注册连接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection,this,_1));
    _server.setMessageCallback(std::bind(&ChatServer::onMessage,this,_1,_2,_3));
    //设置线程数量
    _server.setThreadNum(4);
}
//启动服务
void ChatServer::start() {
    _server.start();
}

//上报连接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr& conn) {
    //客户端断开连接
    if(!conn->connected()) {
        ChatService::instence()->clientCloseException(conn);
        conn->shutdown();
    }
}
//上报读写事件的回调函数
void ChatServer::onMessage(const TcpConnectionPtr& conn,Buffer *buffer,Timestamp time) {
    string buf =buffer->retrieveAllAsString();
    json js = json::parse(buf);
    //达到的目的：完全解耦网络模块代码和业务模块代码
    //通过js["msgid"]:获取一个，业务的handler ->conn js time
    auto msgHandler=ChatService::instence()->getMsgHandler(js["msgid"].get<int>());
    //回调消息绑定好的事件处理，来执行对应的业务处理
    msgHandler(conn,js,time);
}
