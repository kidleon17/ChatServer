#ifndef GROUPMODEL_H
#define GROUPMODEL_H

#include "group.hpp"
#include <string>
#include <vector>
using namespace std;

class GroupModel {
public:
    //创建群组
    bool createGroup(Group &group);
    //加入群组
    void addGroup(int userid,int groupid,string role);
    //删除群组
    void deleteGroup(Group Group);
    //TODO
    vector<Group> queryGroups(int userid);
    //根据指定的groupid查询群组用户的id列表，除了userid自己，主要用户群聊业务给其他群成员群发消息
    vector<int> queryGroupUsers(int userid,int groupid);
};

#endif