#ifndef FRIENDMODEL_H
#define FRIENDMODEL_H

#include<vector>
#include<user.hpp>
using namespace std;
//维护好友信息的操作接口方法
class FriendModel {
public:
    //添加好友关系
    void insert(int userid,int friendid);
    //返回用户的好友列表 frendid -> name -> online
    vector<User> query(int userid);
    //删除好友
    void remove(int userid,int friendid);
    //TODO
};

#endif