#include "chatserver.hpp"
#include "chatservice.hpp"
#include <iostream>
#include <signal.h>
using namespace std;

//处理服务器Ctrl+C后重置用户的状态
void resetHandler(int ) {
    ChatService::instence()->reset();
    exit(0);
}
int main (int argc,char **argv) {
    if(argc<3) {
        cerr<<"Command Invaild example:./ChatServer 127.0.0.1 6000"<<endl;
        exit(-1);
    }
    //解析命令行传输的ip和端口
    char *ip=argv[1];
    uint16_t port = atoi(argv[2]);
    signal (SIGINT,resetHandler);
    EventLoop loop;
    InetAddress addr(ip,port);
    ChatServer server(&loop,addr,"ChatServer");

    server.start();
    loop.loop();
    return 0;
}