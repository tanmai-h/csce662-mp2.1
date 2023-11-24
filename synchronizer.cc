#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <stdlib.h>
#include <unistd.h>
#include <algorithm>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include<glog/logging.h>
#include <stdlib.h>

#include "sns.grpc.pb.h"
#include "coordinator.grpc.pb.h"

namespace fs = std::filesystem;
#define log(severity, msg) LOG(severity) << msg; google::FlushLogFiles(google::severity); 

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ClientContext;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce438::CoordService;
using csce438::ServerInfo;
using csce438::Confirmation;
using csce438::ID;
using csce438::AllSyncServers;
using csce438::SynchService;
using csce438::UserTLFL;
using csce438::AllData;
using csce438::Empty;
using csce438::ID;

int synchID = 1;
std::vector<std::string> get_lines_from_file(std::string);
void run_synchronizer(std::string,std::string,std::string,int);
std::vector<std::string> get_all_users_func(int);
std::vector<std::string> get_tl_or_fl(int synchID, std::string clientID, std::string name);
std::unordered_map<std::string, UserTLFL> others = {};

bool file_exists(const std::string& filename) {
    std::ifstream file(filename);
    bool good = file.good();
    file.close();
    return good;
}

class SynchServiceImpl final : public SynchService::Service {
    Status GetUserTLFL(ServerContext * context, const ID * id, AllData * alldata) override {
        std::cout << " REQ for GetUserTLFL from synchID=" << id->id() << "\n";
        std::vector<std::string> list = get_lines_from_file("./"+std::string("master_")+std::to_string(synchID)+"_currentusers.txt");
        for(auto s:list) {
            UserTLFL usertlfl;
            std::cout << " for current user: " << s << "\n";
            usertlfl.set_user(s);
            std::vector<std::string> tl  = get_tl_or_fl(synchID, s, "tl");
            for (auto s : tl) {
              std::cout << "\t TIMELINE: " << s << "\n";
            }
            std::vector<std::string> flw = get_tl_or_fl(synchID, s, "flw");
            for (auto s : flw) {
              std::cout << "\t FOLLOWING: " << s << "\n";
            }
            std::vector<std::string> flr = get_tl_or_fl(synchID, s, "flr");
            for (auto s : flr) {
              std::cout << "\t FOLLOWER: " << s << "\n";
            }
            std::cout << "\n\n";
            for(auto timeline:tl){
                usertlfl.add_tl(timeline);
            }
            for(auto follow:flw){
                usertlfl.add_flw(follow);
            }
            for (auto flr : flr) {
              usertlfl.add_flr(flr);
            }
            alldata->add_data()->CopyFrom(usertlfl);
        }
        return Status::OK;
    }
};

void RunServer(std::string coordIP, std::string coordPort, std::string port_no, int synchID){
  std::thread t1(run_synchronizer, coordIP, coordPort, port_no, synchID);

  //localhost = 127.0.0.1
  std::string server_address("127.0.0.1:"+port_no);
  SynchServiceImpl service;
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  std::cout << "Server listening on " << server_address << std::endl;
  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
  t1.join();
}

int main(int argc, char** argv) {
  
  int opt = 0;
  std::string coordIP = "localhost";
  std::string coordPort = "9090";
  std::string port = "1234";

  while ((opt = getopt(argc, argv, "h:j:p:n:")) != -1){
    switch(opt) {
      case 'h':
          coordIP = optarg;
          break;
      case 'j':
          coordPort = optarg;
          break;
      case 'p':
          port = optarg;
          break;
      case 'n':
          synchID = std::stoi(optarg);
          break;
      default:
	         std::cerr << "Invalid Command Line Argument\n";
    }
  }

  RunServer(coordIP, coordPort, port, synchID);
  return 0;
}

void run_synchronizer(std::string coordIP, std::string coordPort, std::string port, int synchID) {
    std::string target_str = coordIP + ":" + coordPort;
    std::unique_ptr<CoordService::Stub> coord_stub_;
    coord_stub_ = std::unique_ptr<CoordService::Stub>(CoordService::NewStub(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials())));

    ServerInfo msg;
    Confirmation c;
    ClientContext context;

    msg.set_serverid(synchID);
    msg.set_hostname("127.0.0.1");
    msg.set_port(port);
    msg.set_clusterid(std::to_string(synchID));
    Empty empty;
    Confirmation confirmation;
    ID id;

    grpc::Status status = coord_stub_->RegisterSyncServer(&context, msg, &confirmation);
    if (!status.ok()) {
      log(INFO, "Coord down");
      exit(-1);
    }
    std::cout << " conf: " << confirmation.type() << " " << confirmation.status() << "\n";
    AllSyncServers allsyncservers;
    sleep(10);

    try {
      ClientContext cc;
      coord_stub_->GetSyncServers(&cc, empty, &allsyncservers);
      for (auto s : allsyncservers.servers()) {
        std::cout << " reg " << s.hostname() << " - " << s.port() << " - " << s.type() << " - " << s.clusterid() << "\n";
      }
    } catch(...) {
        std::exception_ptr p = std::current_exception();
        std::cout <<(p ? p.__cxa_exception_type()->name() : "null") << std::endl;
    }

    std::vector<std::unique_ptr<SynchService::Stub>> syncstubs;
    for (const ServerInfo serverInfo : allsyncservers.servers()) {
      if (serverInfo.clusterid() != std::to_string(synchID)) {
        std::cout << " made stub for: " << serverInfo.port() << " - " << serverInfo.clusterid() << "\n";
        std::cout << "\ttarget_str=" << "127.0.0.1:" + serverInfo.port() << "\n";
        syncstubs.push_back(std::move(
          SynchService::NewStub(
              grpc::CreateChannel("127.0.0.1:" + serverInfo.port(), grpc::InsecureChannelCredentials())
            )));
      }
    }

    while(true) {
        AllData all;
        std::vector<std::string> myusers = get_all_users_func(synchID);
        for (auto & stub : syncstubs) {
          ClientContext ctx;
          std::cout << " calling using the stub " << "\n";
          stub->GetUserTLFL(&ctx, id, &all);
          for(const UserTLFL &d : all.data()) {
            std::cout << "\t\tgot data from " << d.user() << "\n";
            if (others.find(d.user()) == others.end() || others[d.user()].tl().size() < d.tl().size() || others[d.user()].flw().size() != d.flw().size() || others[d.user()].flr().size() != d.flr().size()) {
              for (auto flr : d.flr()) {
                  // std::string m = "./master/"+std::to_string(synchID)+"/"+flr + "_timeline";
                  // std::string s = "./slave/" +std::to_string(synchID)+"/"+flr + "_timeline";
                  // int ml = 0, sl = 0;
                  // std::vector<std::string> mt = {}, st = {};
                  // if (file_exists(m)) {
                  //   mt = get_lines_from_file(m);
                  // }
                  // if (file_exists(s)) {
                  //   st = get_lines_from_file(s);
                  // }
                  // std::vector<std::string> towrite;
                  // if (mt.size() > st.size()) {
                  //   towrite = mt;
                  // }
                  // else {
                  //   towrite = st;
                  // }
                  // for (auto line : d.tl()) {
                    
                  // }
              }
            }
            others[d.user()] = d;
          }
        }
      sleep(5);
    }
    return ;
}

std::vector<std::string> get_lines_from_file(std::string filename) {
  std::cout << " opening: " << filename << "\n";
  std::vector<std::string> users;
  std::string user;
  std::ifstream file; 
  file.open(filename);
  if(file.peek() == std::ifstream::traits_type::eof()){
    //return empty vector if empty file
    //std::cout<<"returned empty vector bc empty file"<<std::endl;
    file.close();
    return users;
  }
  while(file){
    getline(file,user);

    if(!user.empty())
      users.push_back(user);
  } 

  file.close();

  return users;
}

bool file_contains_user(std::string filename, std::string user) {
    std::vector<std::string> users;
    //check username is valid
    users = get_lines_from_file(filename);
    for(int i = 0; i<users.size(); i++){
      //std::cout<<"Checking if "<<user<<" = "<<users[i]<<std::endl;
      if(user == users[i]){
        //std::cout<<"found"<<std::endl;
        return true;
      }
    }
    //std::cout<<"not found"<<std::endl;
    return false;
}

std::vector<std::string> get_all_users_func(int synchID){
  
    //read all_users file master and client for correct serverID
    std::string master_users_file = "./master_"+std::to_string(synchID)+"_all_users.txt";
    std::string slave_users_file = "./slave_"+std::to_string(synchID)+"_all_users.txt";
    //take longest list and package into AllUsers message
    std::vector<std::string> master_user_list = get_lines_from_file(master_users_file);
    std::vector<std::string> slave_user_list = get_lines_from_file(slave_users_file);

    if(master_user_list.size() >= slave_user_list.size())
        return master_user_list;
    else
        return slave_user_list;
}

void write_lines_to_file(const std::string& filename, const std::vector<std::string>& lines) {
    std::ofstream file(filename);

    if (file.is_open()) {
        for (const std::string& line : lines) {
            file << line << '\n';
        }

        file.close();
    } else {
        std::cerr << "Unable to open file: " << filename << std::endl;
    }
}

std::vector<std::string> get_tl_or_fl(int synchID, std::string clientID, std::string name){
    std::string master_fn = "./master_"+std::to_string(synchID)+"_" + clientID;
    std::string slave_fn = "./slave_"+std::to_string(synchID)+"_"   +  clientID;
    if(name == "tl") {
        master_fn.append(".txt");
        slave_fn.append(".txt");
    }else if (name == "flw") {
        master_fn.append("_following.txt");
        slave_fn.append("_following.txt");
    } else if (name == "flr") {
        master_fn.append("_follower.txt");
        slave_fn.append("_follower.txt");
    } else if (name == "current") {
        master_fn.append("_currentuser.txt");
        slave_fn.append("_currentuser.txt");
    }
    std::vector<std::string> m = get_lines_from_file(master_fn);
    std::vector<std::string> s = get_lines_from_file(slave_fn);

    if(m.size()>=s.size()){
        return m;
    }else{
        return s;
    }

}