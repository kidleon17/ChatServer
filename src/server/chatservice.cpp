#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <vector>
#include<string>
using namespace std;
using namespace muduo;
//获取单例对象的接口函数
ChatService* ChatService::instence () {
    static ChatService service;
    return &service;
}

//注册消息以及对应的回调函数
ChatService::ChatService() {
    _msgHandlerMap.insert({LOGIN_MSG,std::bind(&ChatService::login,this,_1,_2,_3)});
    _msgHandlerMap.insert({LOGINOUT_MSG,std::bind(&ChatService::loginout,this,_1,_2,_3)});
    _msgHandlerMap.insert({REG_MSG,std::bind(&ChatService::reg,this,_1,_2,_3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG,std::bind(&ChatService::oneChat,this,_1,_2,_3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG,std::bind(&ChatService::addFriend,this,_1,_2,_3)});

    //处理群组的业务的回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG,std::bind(&ChatService::createGroup,this,_1,_2,_3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG,std::bind(&ChatService::addGroup,this,_1,_2,_3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG,std::bind(&ChatService::groupChat,this,_1,_2,_3)});

    //连接Redis服务器
    if(_redis.connect()) {
        //设置上报消息回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage,this,_1,_2));
    }
}

//服务器异常的重置方法
void ChatService::reset() {
    //把所有online的用户重置为offline
    _userModel.resetState();
}
//处理登录业务 ORM 业务层操作的都是对象
void ChatService::login(const TcpConnectionPtr &conn,json& js,Timestamp time) {
    int id=js["id"].get<int>();
    string pwd = js["password"];
    User user =_userModel.query(id);
    if(user.getId()==id&&user.getPwd()==pwd) {
        if(user.getState()=="online") {
        //该用户已经登录不允许重复登录
        json response;
        response["msgid"]=LOGIN_MSG_ACK;
        response["errno"]=2;
        response["errmsg"]="This account is already login,please check and try annother account!";
        conn->send(response.dump()); 
        } else {
            //登录成功，记录用户连接信息,保证线程安全，利用作用域
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id,conn});
            }
            //登录成功向Redis订阅channel(id)
            _redis.subscribe(id);

            //登录成功，更新用户的状态信息 state offline->online
            user.setState("online");
            _userModel.updateState(user);
            json response;
            response["msgid"]=LOGIN_MSG_ACK;
            response["errno"]=0;
            response["id"]=user.getId();
            response["name"] = user.getName();
            //查询用户是否有离线消息
            vector<string>vec=_offlineMsgModel.query(id);
            if(!vec.empty()) {
                response["offlinemsg"]=vec;
                //读取该用户的离线消息后，删除其离线消息
                _offlineMsgModel.remove(id);
            }
            //查询用户的好友信息返回
            vector<User> uservec=_friendModel.query(id);
            if(!uservec.empty()) {
                vector<string> vec2;
                for(User &user:uservec) {
                    json js;
                    js["id"]=user.getId();
                    js["name"]=user.getName();
                    js["state"]=user.getState(); 
                    vec2.push_back(js.dump());               
                }
                response["friends"]=vec2;
            }
            vector<Group> groupvec=_groupModel.queryGroups(id);
            if(!groupvec.empty()) {
                vector<string>vec3;
                for(Group &group:groupvec) {
                    json js;
                    js["groupid"]=group.getId();
                    js["groupname"]=group.getName();
                    js["gropdesc"]=group.getDesc();
                    vector<string> UserV;
                    for(GroupUser &user:group.getUsers()) {
                        json gjs;
                        gjs["id"]=user.getId();
                        gjs["state"]=user.getState();
                        gjs["name"]=user.getName();
                        gjs["role"]=user.getRole();
                        UserV.push_back(gjs.dump());
                    }
                    js["users"]=UserV;
                    vec3.push_back(js.dump());
                }
                response["groups"]=vec3;
            }
            conn->send(response.dump());
        }
    } else {
        //该用户不存在失败或者密码错误
        json response;
        response["msgid"]=LOGIN_MSG_ACK;
        response["errno"]=1;
        response["errmsg"]="id or password is invalid";
        conn->send(response.dump()); 
    }
}

//处理注册业务
void ChatService::reg(const TcpConnectionPtr &conn,json& js,Timestamp time) {
    string name = js["name"];
    string pwd = js["password"];
    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if(state) {
        //注册成功
        json response;
        response["msgid"]=REG_MSG_ACK;
        response["errno"]=0;
        response["id"]=user.getId();
        conn->send(response.dump());
    } else {
        //注册失败
        json response;
        response["msgid"]=REG_MSG_ACK;
        response["errno"]=1;
        conn->send(response.dump());
    }
}
//处理一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn,json& js,Timestamp time) {
    int toid = js["toid"].get<int>();
    //标识用户是否在线
    {
        lock_guard<mutex> lock(_connMutex);
        auto it=_userConnMap.find(toid);
        if(it!=_userConnMap.end()) {
            //toid 在线转发消息,服务器主动推送消息给toid用户
            it->second->send(js.dump());
            return;
        }
    }

    //查询用户是否在线
    User user=_userModel.query(toid);
    if(user.getState()=="online") {
        //toid 在其他的服务器上，发布消息
        _redis.publish(toid,js.dump());
        return;
    }
    //toid 不在线，存储离线消息
    _offlineMsgModel.insert(toid,js.dump());
}
//添加好友业务
void ChatService::addFriend(const TcpConnectionPtr&conn,json& js,Timestamp time) {
    int userid=js["id"].get<int>();
    int friendid=js["friendid"].get<int>();
    //存储好友信息
    _friendModel.insert(userid,friendid);
}
//处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn) {
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for(auto it=_userConnMap.begin();it!=_userConnMap.end();it++) {
            if(it->second==conn) {
                //从map表删除用户的连接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    //用户退出，取消订阅
    _redis.unsubscribe(user.getId());

    //更新用户状态
    if(user.getId()!=-1) {
        user.setState("offline");
        _userModel.updateState(user);
    }
}
//处理客户端注销业务
void ChatService::loginout(const TcpConnectionPtr&conn,json& js,Timestamp time) {
    int userid=js["id"].get<int>();  
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if(it!=_userConnMap.end()) {
            _userConnMap.erase(it);
        }
    }

    //用户注销，取消订阅通道
    _redis.unsubscribe(userid);
    //更新用户的信息
    User user(userid,"","","offline");
    _userModel.updateState(user);
}
//创建群组业务
void ChatService::createGroup(const TcpConnectionPtr&conn,json& js,Timestamp time) {
    int userid=js["id"].get<int>();
    string name=js["groupname"];
    string desc=js["groupdesc"];
    Group group(-1,name,desc);
    if(_groupModel.createGroup(group)) {
        //存储群组创建人的信息
        _groupModel.addGroup(userid,group.getId(),"creator");
    }
}
    //加入群组业务
void ChatService::addGroup(const TcpConnectionPtr&conn,json& js,Timestamp time) {
    int userid = js["id"].get<int>();
    int groupid = js["group"].get<int>();
    _groupModel.addGroup(userid,groupid,"normal");
}
    //群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr&conn,json& js,Timestamp time) {
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupModel.queryGroupUsers(userid,groupid);

    lock_guard<mutex> lock(_connMutex);
    for(int id:useridVec) {
        auto it =_userConnMap.find(id);
        if(it!=_userConnMap.end()) {
            //转发群消息
            it->second->send(js.dump());
        }else {
            User user=_userModel.query(id);
            if(user.getState()=="online") {
                //转发
                _redis.publish(user.getId(),js.dump());
            }else {
                //存储离线消息
                _offlineMsgModel.insert(id,js.dump());        
            }
        }
    }
}

void ChatService::handleRedisSubscribeMessage (int userid ,string msg) {
    lock_guard<mutex> lock(_connMutex);
    auto it =_userConnMap.find(userid);
    if(it!=_userConnMap.end()) {
        it->second->send(msg);
        return ;
    }

    //存储用户离线消息,在通道中取消息的时候下线了
    _offlineMsgModel.insert(userid,msg);
}
MsgHandler ChatService::getMsgHandler(int msgid) {
    //记录错误日志，msgid没有对应的事件处理回调
    auto it =_msgHandlerMap.find(msgid);
    if(it==_msgHandlerMap.end()) {
        //返回一个空操作
        return [=](const TcpConnectionPtr &conn,json js,Timestamp time) {
            LOG_ERROR <<"msgid: "<<msgid<<" can not find handler";
        };
    } else {
        return _msgHandlerMap[msgid];
    }
}