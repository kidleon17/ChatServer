#include"json.hpp"
#include<iostream>
#include<thread>
#include<string>
#include<vector>
#include<chrono>
#include<ctime>
using namespace std;
using json=nlohmann::json;

#include<unistd.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<arpa/inet.h>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

//记录当前用户的登录信息
User g_currentUser;
//记录当前登录用户的好友列表
vector<User> g_currentUserFriendList;
//记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;

//显示当前登录成功用户的基本信息
void showCurrentUserData();

//接收线程
void ReadTaskHandler(int clientfd);

//获取当前时间
string getCurrentTime();

//主聊天页面程序
bool isMainMenuRunning =false;
void mainMenu(int clientfd);

//控制主菜单页面
//聊天客户端的主实现，main作为主线程，发送线程，子线程作为接收线程
int main(int argc,char **argv) {
    if(argc<3) {
        cerr<<"Command Invaild example:./ChatClient 127.0.0.1 6000"<<endl;
        exit(-1);
    }

    //解析命令行传输的ip和端口
    char *ip=argv[1];
    uint16_t port = atoi(argv[2]);

    //创建client的socket
    int clientfd=socket(AF_INET,SOCK_STREAM,0);
    if(-1==clientfd) {
        cerr<<"socket create error"<<endl;
        exit(-1);
    }

    //填写client需要连接的server信息，ip+port
    sockaddr_in server;
    memset(&server,0,sizeof(sockaddr_in));

    server.sin_family=AF_INET;
    server.sin_port=htons(port);
    server.sin_addr.s_addr=inet_addr(ip);

    //client连接服务器
    if(-1==connect(clientfd,(sockaddr*)&server,sizeof(sockaddr_in))) {
        cerr<<"socket connect error"<<endl;
        close(clientfd);
        exit(-1);
    }

    //main线程用来处理用户的输入,负责发送数据
    for(;;) {
        //显示首页的页面菜单
        cout<<"============================="<<endl;
        cout<<"1.login"<<endl;
        cout<<"2.regsiter"<<endl;
        cout<<"3.quit"<<endl;
        cout<<"choice: ";
        cout<<"============================="<<endl;
        int choice=0;
        cin>>choice;
        cin.get();//去掉缓冲区的回车
        switch (choice) {
            case 1: {//1、login
                int id=0;
                char pwd[50]={0};
                cout<<"userid:"<<endl;
                cin>>id;
                cin.get();
                cout<<"userpassword:"<<endl;
                cin.getline(pwd,50);

                json js;
                js["msgid"]=LOGIN_MSG;
                js["id"]=id;
                js["password"]=pwd;
                string request = js.dump();

                int len = send(clientfd,request.c_str(),strlen(request.c_str())+1,0);
                if(len==-1) {
                    cerr<<"send login in msg error:"<<request<<endl;
                }else {
                    char buffer[1024]={0};
                    len=recv(clientfd,buffer,1024,0);
                    if(-1==len) {
                        cerr<<"recv login respose error"<<endl;
                    }else {
                        json responsejs=json::parse(buffer);
                        if(0!=responsejs["errno"].get<int>()) {//登录失败
                            cerr<<responsejs["errmsg"]<<endl;
                        } else {
                            //登录成功
                            //记录信息
                            //用户的id和name
                            g_currentUser.setId(responsejs["id"].get<int>());
                            g_currentUser.setName(responsejs["name"]);

                            //记录好友列表
                            if(responsejs.contains("friends")) {
                                //初始化
                                g_currentUserFriendList.clear();
                                vector<string> vec =responsejs["friends"];
                                for(auto &str:vec) {
                                    json js=json::parse(str);
                                    User user;
                                    user.setId(js["id"].get<int>());
                                    user.setName(js["name"]);
                                    user.setState(js["state"]);
                                    g_currentUserFriendList.push_back(user);
                                }
                            }

                            //记录群组信息
                            if(responsejs.contains("groups")) {
                                //初始化
                                g_currentUserGroupList.clear();
                                vector<string> vec=responsejs["groups"];
                                for(auto &str:vec) {
                                    json grpjs = json::parse(str);
                                    Group group;
                                    group.setId(grpjs["groupid"].get<int>());
                                    group.setName(grpjs["groupname"]);
                                    group.setDesc(grpjs["gropdesc"]);
                                    
                                    vector<string>vec2=grpjs["users"];
                                    for(auto userstr:vec2) {
                                        GroupUser user;
                                        json js=json::parse(userstr);
                                        user.setId(js["id"]);
                                        user.setName(js["name"]);
                                        user.setState(js["state"]);
                                        user.setRole(js["role"]);
                                        group.getUsers().push_back(user);
                                    }
                                    g_currentUserGroupList.push_back(group);
                                }
                            }
                            showCurrentUserData();

                            //显示离线消息
                            if(responsejs.contains("offlinemsg")) {
                                vector<string> vec=responsejs["offlinemsg"];
                                for (string &str:vec) {
                                    json js=json::parse(str);
                                    int msgtype=js["msgid"].get<int>();
                                    if(ONE_CHAT_MSG==msgtype) {
                                        cout<<js["time"].get<string>()<<"["<<js["id"]<<"]"<<js["name"]<<"said: "<<js["msg"].get<string>()<<endl;
                                    }else {
                                        cout<<"group message "<<js["groupid"]<<js["time"].get<string>()<<"["<<js["id"]<<"]"<<js["name"]<<"said: "<<js["msg"].get<string>()<<endl;
                                    }
                                }
                            }

                            //登录成功启动接收线程接收数据
                            static int readthreadnumber = 0;
                            if(readthreadnumber==0) {
                                std::thread readTask(ReadTaskHandler,clientfd);
                                readTask.detach();//该线程只需要启动一次
                                readthreadnumber++;
                            }
                            //进入主聊天界面
                            isMainMenuRunning=true;
                            mainMenu(clientfd);
                        }
                    }
                }
                break;
            }
            case 2: {
                char name[50];
                cout<<"username:"<<endl;
                cin.getline(name,50);
                char pwd[50];
                cout<<"password"<<endl;
                cin.getline(pwd,50);

                json js;
                js["msgid"]=REG_MSG;
                js["name"]=name;
                js["password"]=pwd;
                string request=js.dump();

                int len=send(clientfd,request.c_str(),strlen(request.c_str())+1,0);
                if(len==-1) {
                    cerr<<"send reg msg error: "<<request<<endl;
                }else {
                    char buffer[1024]={0};
                    len = recv(clientfd,buffer,1024,0);
                    if(len==-1) {
                        cerr<<"recv response error!"<<endl;
                    }else {
                        json responsejs = json::parse(buffer);
                        if(0!=responsejs["errno"].get<int>()) {//注册失败
                            cerr<<name<<"is already exisit, register error"<<endl;
                        }else {
                            //注册成功
                            cout<<name<<"register success, userid is "<<responsejs["id"]<<"please remember it"<<endl;
                        }
                    }
                }
                break;
            }
            case 3: {//退出业务
                close(clientfd);
                exit(0);
                break;
            }
            default:{
                cerr<<"invalid input"<<endl;
            }
        };
    }

}

void showCurrentUserData() {
    cout<<"========================================================================="<<endl;
    cout<<"Current Login User id: "<<g_currentUser.getId()<<"name:"<<g_currentUser.getName()<<endl;
    cout<<"------------------------------Friend_List------------------------------"<<endl;
    if(!g_currentUserFriendList.empty()) {
        for(User &user:g_currentUserFriendList) {
            cout<<user.getId()<<" "<<user.getName()<<" "<<user.getState()<<endl;
        }
    }
    cout<<"-------------------------------Group_List--------------------------------"<<endl;
    if(!g_currentUserGroupList.empty()) {
        for(Group &group:g_currentUserGroupList) {
            cout<<group.getId()<<" "<<group.getName()<<" "<<group.getDesc()<<endl;
            for(GroupUser user:group.getUsers()) {
                cout<<user.getId()<<" "<<user.getName()<<" "<<user.getState()<<" "<<user.getRole()<<endl;
            }
        }
    }
    cout<<"========================================================================="<<endl;
}

void ReadTaskHandler(int clientfd) {
    for(;;) {
        char buffer[1024]={0};    
        int len=recv(clientfd,buffer,1024,0);//阻塞了
        if(-1==len) {
            close(clientfd);
            exit(-1);
        }
        
        //接收ChatServer转发的JSON
        json js = json::parse(buffer);
        int msgtype=js["msgid"].get<int>();
        if(ONE_CHAT_MSG==msgtype) {
            cout<<js["time"].get<string>()<<"["<<js["id"]<<"]"<<js["name"]<<"said: "<<js["msg"].get<string>()<<endl;
            continue;
        }else {
            cout<<"group message "<<js["groupid"]<<" "<<js["time"].get<string>()<<"["<<js["id"]<<"]"<<js["name"]<<"said: "<<js["msg"].get<string>()<<endl;
            continue;
        }
    }
}
string getCurrentTime() {
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}
void help(int clientfd =0,string str="");
void chat(int clientfd =0,string str="");
void addfriend(int clientfd =0,string str="");
void creategroup(int clientfd =0,string str="");
void addgroup(int clientfd =0,string str="");
void groupchat(int clientfd =0,string str="");
void loginout(int clientfd =0,string str="");
unordered_map<string,string> commandMap={
    {"help","get the help information,format:help"},
    {"chat","chat with another user,format: chat:friendid:message"},
    {"addfrined","addfriend by using id,format: addfriend:friendid"},
    {"creategroup","Create a Group by name and descption,format: creategroup:groupname:groupdesc"},
    {"addgroup","join a group by group id,format: addgroup:groupid"},
    {"groupchat","chat with the groupusers,format:groupchat:message"},
    {"loginout","exit,format:loginout"}
};

//注册客户端支持的用户命令处理函数

unordered_map<string,function<void(int,string)>>commandHandlerMap = {
    {"help",help},
    {"chat",chat},
    {"addfriend",addfriend},
    {"addgroup",addgroup},
    {"creategroup",creategroup},
    {"groupchat",groupchat},
    {"loginout",loginout}
};
void mainMenu(int clientfd) {
    help();

    char buffer[1024]={0};
    while(isMainMenuRunning) {
        cin.getline(buffer,1024);
        string commandbuf(buffer);
        string command;
        int idx=commandbuf.find(':');
        if(-1==idx) {
            command=commandbuf;
        }else {
            command = commandbuf.substr(0,idx);
        }
        auto it=commandHandlerMap.find(command);
        if(it==commandHandlerMap.end()) {
            cerr<<"invalid input command"<<endl;
            continue;
        }
        //调用指令相应的函数处理
        it->second(clientfd,commandbuf.substr(idx+1,commandbuf.size()-idx));//调用对应的方法
    }
}
//系统支持的命令列表
void help(int clientfd,string str) {
    cout<<"show command list >>>>"<<endl;
    for(auto &p:commandMap) {
        cout<<p.first<<":"<<p.second<<endl;
    }
    cout<<endl;
}
void chat(int clientfd,string str) {
    int idx=str.find(":");
    if(idx==-1) {
        cerr<<"chat command invaild "<<endl;
        return;
    }
    int friendid=stoi(str.substr(0,idx));
    string messages = str.substr(idx+1,str.size()-idx);

    json js;
    js["msgid"]=ONE_CHAT_MSG;
    js["id"]=g_currentUser.getId();
    js["name"]=g_currentUser.getName();
    js["toid"]=friendid;
    js["msg"]=messages;
    js["time"] = getCurrentTime();
    string buffer=js.dump();
    int len =send(clientfd,buffer.c_str(),strlen(buffer.c_str())+1,0);
    if(len==-1) {
        cerr<<"send chat msg error"<<endl;
    }else {
        cout<<"send msg success!"<<endl;
    }
}
void addfriend(int clientfd,string str) {
    int friendid=stoi(str);
    json js;
    js["msgid"]=ADD_FRIEND_MSG;
    js["id"]=g_currentUser.getId();
    js["friendid"]=friendid;
    string buffer=js.dump();
    int len=send(clientfd,buffer.c_str(),strlen(buffer.c_str())+1,0);
    if(-1==len) {
        cerr<<"send addfriend Msg error"<<buffer<<endl; 
    } else {
        cout<<"addfriend success!"<<endl;
    }

}
void creategroup(int clientfd,string str) {
    int idx=str.find(':');
    string name;
    string desc;
    string buffer;
    if(idx==-1) {
        name=str;
        desc="This group does not have a desc!";
    } else {
        name=str.substr(0,idx);
        desc=str.substr(idx+1,str.size()-idx);
    }
    json js;
    js["msgid"]=CREATE_GROUP_MSG;
    js["id"]=g_currentUser.getId();
    js["groupname"]=name;
    js["groupdesc"]=desc;
    buffer=js.dump();
    int len=send(clientfd,buffer.c_str(),strlen(buffer.c_str())+1,0);
    if(-1==len) {
            cerr<<"send creategroup Msg error"<<buffer<<endl; 
    } else {
            cout<<"creategroup success!"<<endl;
    }
}
void addgroup(int clientfd,string str) {
    int groupid=atoi(str.c_str());
    json js;
    js["msgid"]=ADD_GROUP_MSG;
    js["id"]=g_currentUser.getId();
    js["group"]=groupid;
    
    string buffer=js.dump();

    int len=send(clientfd,buffer.c_str(),strlen(buffer.c_str())+1,0);
    if(-1==len) {
        cerr<<"send addgroup Msg error"<<buffer<<endl; 
    } else {
        cout<<"addgroup success!"<<endl;
    }
}
void groupchat(int clientfd,string str) {
    int idx=str.find(':');
    if(idx==-1) {
        cerr<<"invaild group chat command"<<endl;
        return;
    }
    int groupid=atoi(str.substr(0,idx).c_str());
    string messages=str.substr(idx+1,str.size()-idx);
    json js;
    js["msgid"]=GROUP_CHAT_MSG;
    js["name"]=g_currentUser.getName();
    js["id"]=g_currentUser.getId();
    js["groupid"]=groupid;
    js["msg"]=messages.c_str();
    js["time"] = getCurrentTime();
    
    string buffer=js.dump();
    int len=send(clientfd,buffer.c_str(),strlen(buffer.c_str())+1,0);
    if(-1==len) {
        cerr<<"send groupchat Msg error"<<buffer<<endl; 
    } else {
        cout<<"groupchat success!"<<endl;
    }
}
void loginout(int clientfd,string str) {
    json js;
    js["msgid"]=LOGINOUT_MSG;
    js["id"]=g_currentUser.getId();
    string buffer=js.dump();
    int len=send(clientfd,buffer.c_str(),strlen(buffer.c_str())+1,0);
    if(-1==len) {
        cerr<<"send loginout Msg error"<<buffer<<endl; 
    } else {
        cout<<"loginout success!"<<endl;
        isMainMenuRunning=false;
    }
}