#include<iostream>
#include<string>
#include<stdio.h>
#include<string.h>
#include<sstream>
#include<unistd.h>
#include<signal.h>
#include<algorithm>
#include<vector>
#include<sys/wait.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<map>
#include<arpa/inet.h>
#include<dirent.h>
using namespace std;


/*
commandType
0 : init
1 : build in command
2 : operation command
*/

/*
operationType
0 : init
1 : |
2 : >
3 : |1
4 : ?1
5 : >1
6 : <1
*/

extern char** environ;
class command
{
public:
    int commandType;
    int previosOP;
    int nextOP;
    int readUserID;
    int writeUserID;
    int userPipeErrorType;
    string redirectFileName;
    string currentCommand;
    string wholecommand;
    vector<string> commandArgument;
    vector<string> tokens;

    bool isNumberPipe(string token);
    bool isUserPipe(string token);
    bool isNormalPipe(string token);
    bool isOPToken(string token);
    void setNextOP(string token);
    char **buildArgv();
    command(/* args */);
    ~command();
};

command::command(/* args */)
{
    currentCommand = "";
    redirectFileName = "";
    wholecommand = "";
    commandType = 0;
    nextOP = 0;
    previosOP = 0;
    userPipeErrorType = 0;
    readUserID = -1;
    writeUserID = -1;
}

command::~command()
{
    tokens.clear();
    commandArgument.clear();
}

bool command::isNumberPipe(string token){
    if(token.size()<2) return false;
    return (token[0]=='|'||token[0]=='!')&&isdigit(token[1]);
}

bool command::isUserPipe(string token){
    if(token.size()<2) return false;
    return (token[0]=='<'||token[0]=='>')&&isdigit(token[1]);
}

bool command::isNormalPipe(string token){
    if(token == "|" || token == ">") return true;
    else if((token[0]=='|' || token[0]=='!')&&isdigit(token[1])) return true;
    return false;
}

bool command::isOPToken(string token){
    if(token == "|" || token == ">"){
        return true;
    }else if(token.size() >= 2 &&
     (token[0] == '|' || token[0] == '!' || token[0] == '>' || token[0] == '<') &&
     isdigit(token[1])){
        return true;
    }

    return false;
}

void command::setNextOP(string token){
    if(token == "|"){
        nextOP = 1;
    } else if(token == ">"){
        nextOP = 2;
    } else if(token.size()>=2){
        if(token[0] == '|' && isdigit(token[1])) nextOP = 3;
        else if(token[0] = '!' && isdigit(token[1])) nextOP = 4;
        else if(token[0] = '>' && isdigit(token[1])) nextOP = 5;
        else if(token[0] = '<' && isdigit(token[1])) nextOP = 6;
    } 
}

char **command::buildArgv(){
    int size = commandArgument.size() + 2;
    char **argv = new char*[size];
    argv[0] = new char[currentCommand.size()];
    strcpy(argv[0], currentCommand.c_str());
    for(int i=1;i<size-1;++i){
        argv[i] = new char[commandArgument[i].size()];
        strcpy(argv[i], commandArgument[i-1].c_str());
    }
    argv[size - 1] = NULL;
    return argv;
}

struct pipestruct
{
    pipestruct() : numberleft(0), pipetype(0) {}
    int numberleft; 
    int pipetype; // 0 : init, 1 : | , 2 : ! , 3 : |1 , 4 : !1
    int fd[2];
};

struct userpipestruct
{
    userpipestruct() : pipetype(0), senderUserID(-1), receiverUserID(-1) {}
    int senderUserID;
    int receiverUserID;
    int pipetype; // 0 : init, 1 : | , 2 : ! , 3 : |1 , 4 : !1
    int fd[2];
};

class userinfo
{
public:
    int ID;
    int ssock;
    int port;
    string name;
    string addr;
    map<string, string> env;
    vector<pipestruct> userNumberPipes;

    userinfo(/* args */);
    ~userinfo();
};

userinfo::userinfo(/* args */)
{
    ID = -1;
    env["PATH"] = "bin:.";
}

userinfo::~userinfo()
{
    env.clear();
    userNumberPipes.clear();
}

vector<pipestruct> pipes;
vector<pipestruct> numberPipes;
vector<userpipestruct> userPipes;
vector<userinfo> users;

map<string, string> backupenv;

bool idused[30] = {false};
const int maxProcessNum = 150;
const string welcomemessage = "****************************************\n** Welcome to the information server. **\n****************************************\n";
const int FD_NULL = open("/dev/null", O_RDWR);
int processNum = 0;
int maxfd = 50;
int serverport;

int stdinfdTmp , stdoutTmp , stderrTmp;

fd_set afds;
fd_set rfds;

void signal_terminate(int signal){
    if(signal != SIGINT) return;

    kill(0, SIGQUIT);
    while(waitpid(-1, NULL,WNOHANG) > 0) ;
    exit(0);
}

void signal_child(int signal){
	int status;
	//while(waitpid(-1,&status,WNOHANG) > 0){}
    while(waitpid(-1,&status,WNOHANG) > 0){ --processNum; } // add
}

bool isBuildinCmd(command currentcmd){
    return currentcmd.tokens[0] == "setenv" || currentcmd.tokens[0] == "printenv" 
        || currentcmd.tokens[0] == "exit" || currentcmd.tokens[0] == "who"
        || currentcmd.tokens[0] == "tell" || currentcmd.tokens[0] == "yell"
        || currentcmd.tokens[0] == "name";
}

bool existUserPipe(int senderUserID, int receiverUserID){
    for(int i=0;i<userPipes.size();++i){
        if(userPipes[i].senderUserID == senderUserID && userPipes[i].receiverUserID == receiverUserID) return true;
    }
    return false;
}

bool compareByID(const userinfo &a, const userinfo &b)
{
    return a.ID < b.ID;
}

int getUserIndexFromID(int userID){
    int index = -1;
    for(int i=0;i<users.size();++i){
        if(users[i].ID == userID){
            index = i;
            break;
        }
    }
    return index;
}

int getUserPipeIndex(int senderID, int receiverID){
    int index = -1;
    for(int i=0;i<userPipes.size();++i){
        if(userPipes[i].senderUserID == senderID && userPipes[i].receiverUserID == receiverID){
            index = i;
            break;
        }
    }
    return index;
}

int matchNumberPipeQueue(int left){
    for(int i=0;i<numberPipes.size();++i){
        if(left == numberPipes[i].numberleft) return i;
    }
    return -1;
}

void decreaseNumberPipeLeft(){
    for(int i = 0;i<numberPipes.size();++i){
        --numberPipes[i].numberleft;
        /*if(numberPipes[i].numberleft < 0){
            numberPipes.erase(numberPipes.begin()+i);
            --i;
        } */
    }
}

void setUserEnv(int index){
    /*for(map<string, string>::iterator it = users[index].env.begin(); it!=users[index].env.end();++it){
        setenv(it->first.c_str(), it->second.c_str(), 1);
    }*/

    clearenv();
    for(auto& [key, value]:backupenv){
        setenv(key.c_str(), value.c_str(), 1);
    }

    for(auto& [key, value]:users[index].env){
        setenv(key.c_str(), value.c_str(), 1);
    }
}

void broadcast(string message){
    for(int i=0;i<users.size();++i){
        if(write(users[i].ssock, message.c_str(),message.size())<0){
            cerr << "write error" << strerror(errno) <<"\n";
        }
    }
}

void removeUserPipe(int userID){
    for(int i=0;i<userPipes.size();++i){
        if(userPipes[i].senderUserID == userID || userPipes[i].receiverUserID == userID){
            close(userPipes[i].fd[0]);
            userPipes.erase(userPipes.begin()+i);
            --i;
        }
    }
}

void unicast(int srcUserIndex, int dstID, string tellMessage){
    bool exist = false;
    string message;
    int i;
    for(i = 0;i<users.size();++i){
        if(users[i].ID == dstID){
            exist = true;
            break;
        } 
    }
    
    if(exist){
        message = "*** " + users[srcUserIndex].name + " told you ***: " + tellMessage + "\n";
        if(write(users[i].ssock, message.c_str(),message.size())<0){
            cerr << "write error" << strerror(errno) <<"\n";
        }
    }else{
        message = "*** Error: user #" + to_string(dstID) + " does not exist yet. ***\n";
        if(write(users[srcUserIndex].ssock, message.c_str(),message.size())<0){
            cerr << "write error" << strerror(errno) <<"\n";
        }
    }

}

void name(int userIndex, string newName){
    bool exist = false;
    string message;
    int i;
    for(i = 0; i < users.size();++i){
        if(users[i].name == newName){
            exist = true;
            break;
        } 
    }
    if(exist){
        message = "*** User '" + newName + "' already exists. ***\n";
        if(write(users[userIndex].ssock, message.c_str(),message.size())<0){
            cerr << "write error" << strerror(errno) <<"\n";
        }
    }else{
        users[userIndex].name = newName;
        message = "*** User from " + users[userIndex].addr + ":" + to_string(users[userIndex].port) + " is named '" + newName + "'. ***\n";
        broadcast(message);
    }
}

void who(int userIndex){
    string message;

    message = "<ID>	<nickname>	<IP:port>	<indicate me>\n";
    if(write(users[userIndex].ssock, message.c_str(),message.size())<0){
        cerr << "write error" << strerror(errno) <<"\n";
    }
    for(int j=0;j<users.size();++j){
        message = to_string(users[j].ID) + "	" + users[j].name + "	" + users[j].addr + ":" + to_string(users[j].port);
        if(j==userIndex) message += "	<-me\n";
        else message += "\n";
        if(write(users[userIndex].ssock, message.c_str(), message.size())<0){
            cerr << "write error" << strerror(errno) <<"\n";
        }
    }
}

void welcome(int userID){
    int i;
    for(i = 0; i < users.size();++i){
        if(users[i].ID == userID){
            if(write(users[i].ssock, welcomemessage.c_str(),welcomemessage.size())<0){
                cerr << "write error" << strerror(errno) <<"\n";
            }
            break;
        }
    }
    
    string broadcastMessage = "*** User '" + users[i].name + "' entered from " + users[i].addr + ":" + to_string(users[i].port) + ". ***\n";
    broadcast(broadcastMessage);
}

void logout(int &userIndex){
    string message = "*** User '" + users[userIndex].name + "' left. ***\n";
    for(int i=0;i<users.size();++i){
        if(i!=userIndex){
            if(write(users[i].ssock, message.c_str(),message.size())<0){
                cerr << "write error" << strerror(errno) <<"\n";
            }
        }
    }
}

void dup2toclient(int userIndex){
    dup2(users[userIndex].ssock, STDIN_FILENO);
    dup2(users[userIndex].ssock, STDOUT_FILENO);
    dup2(users[userIndex].ssock, STDERR_FILENO);
}

void dup2recovery(){
    dup2(stdinfdTmp, STDIN_FILENO);
    dup2(stdoutTmp, STDOUT_FILENO);
    dup2(stderrTmp, STDERR_FILENO);
    /*cout << stdinfdTmp << endl;
    cout << stdoutTmp << endl;
    cout << stderrTmp << endl;*/
    close(stdinfdTmp);
    close(stdoutTmp);
    close(stderrTmp);
}

void forkandexec(command &cmd, int left, int &userIndex){

    RE:
    while(processNum >= maxProcessNum);
    int pid = fork();
    if(pid < 0) {
        
        int status = 0;
		while(waitpid(-1,&status,WNOHANG) > 0){ --processNum; }
        goto RE;
    }else if(pid == 0) { // chld process

        for(int i=0;i<numberPipes.size();++i){
            //cout << "i:" << i << " size:" << numberPipes.size() << "left:" << numberPipes[i].numberleft << "\n";
            if(numberPipes[i].numberleft == 0){
                //cout << "i:" << i << " size:" << numberPipes.size() << "\n";
                //cout << "read:" << numberPipes[i].fd[0] << " write:" << numberPipes[i].fd[1] << "\n";
                //cout << "previous" << cmd.previosOP << " Next:" << cmd.nextOP << "\n";
                close(numberPipes[i].fd[1]);
                dup2(numberPipes[i].fd[0], STDIN_FILENO);
                close(numberPipes[i].fd[0]); 
            }
        }
        //cout << "previos:" << cmd.previosOP <<" next:" <<cmd.nextOP<<endl;
        if(cmd.previosOP == 6 && (cmd.nextOP >= 1 && cmd.nextOP <=5)){
            if(cmd.userPipeErrorType == 0){
                int userPipeIndex = getUserPipeIndex(cmd.writeUserID, users[userIndex].ID);
                //cout << "sender: " << userPipes[userPipeIndex].senderUserID << " receiver:" <<userPipes[userPipeIndex].receiverUserID << endl;
                dup2(userPipes[userPipeIndex].fd[0], STDIN_FILENO);
                close(userPipes[userPipeIndex].fd[0]);
                userPipes.erase(userPipes.begin()+userPipeIndex);
                cmd.previosOP = 0;
            } else {
                dup2(FD_NULL, STDIN_FILENO);
                cmd.previosOP = 0;
            }
        }


        if(cmd.previosOP == 0 && cmd.nextOP != 0) { // ex. 'ls' | ls
            
            if( pipes.size() == 0 && (cmd.nextOP == 3 || cmd.nextOP == 4) && left != 0){ // 'ls' |2
                int index = matchNumberPipeQueue(left);
                //cout << "index:" << index << endl;
                if(index != -1){
                    //cout <<"numberPipes:" <<numberPipes[index].fd[0] <<" " <<numberPipes[index].fd[1]<<endl;
                    dup2(numberPipes[index].fd[1], STDOUT_FILENO);
                    if(cmd.nextOP == 4) dup2(numberPipes[index].fd[1], STDERR_FILENO);
                    close(numberPipes[index].fd[1]);
                    close(numberPipes[index].fd[0]);
                }
            }else if(pipes.size() == 0 && cmd.nextOP == 2){ // 'ls' > a.html
                //cout << cmd.redirectFileName << endl;
                int filefd = open(cmd.redirectFileName.c_str(), O_TRUNC | O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
                dup2(filefd, STDOUT_FILENO);
                close(filefd);
                //cout << "case2" <<endl;
            }else if(cmd.nextOP==5){
                if(cmd.userPipeErrorType==0){
                    int userPipeIndex = getUserPipeIndex(users[userIndex].ID ,cmd.readUserID);
                    //cout << "\nuserPipeIndex" << userPipeIndex << "\n";
                    dup2(userPipes[userPipeIndex].fd[1], STDOUT_FILENO);
                    close(userPipes[userPipeIndex].fd[0]);
                    close(userPipes[userPipeIndex].fd[1]);
                } else {
                    dup2(FD_NULL, STDOUT_FILENO);
                }
            } else { // not appear previosOP == 0 , nextOP == 3(4) this case, because pipes.size() will == 0  ex. ls |
                // ex. 'ls' | cat
                //cout << "case 0 1\n";
                dup2(pipes[pipes.size()-1].fd[1], STDOUT_FILENO);
                close(pipes[pipes.size()-1].fd[1]);
                close(pipes[pipes.size()-1].fd[0]);
            }
        } else if(cmd.previosOP != 0 && cmd.nextOP == 0){ // previosOP == 1 , nextOP == 0   ex. ls | 'ls'
            if(cmd.previosOP==6){ // ls <2
                if(cmd.userPipeErrorType==0){
                    int userPipeIndex = getUserPipeIndex(cmd.writeUserID, users[userIndex].ID);
                    
                    //cout << "\nuserPipeIndex:" << userPipeIndex << "\n";
                    //cout << "userPipeSize:" << userPipes.size() << "\n";
                    //cout << "\nsender:" << cmd.readUserID << "\n";
                    dup2(userPipes[userPipeIndex].fd[0], STDIN_FILENO);
                    close(userPipes[userPipeIndex].fd[0]);
                } else {
                    dup2(FD_NULL, STDIN_FILENO);
                }
            }else{
                //cout << "case 1 0\n";
                //cout << "pipes size:" << pipes.size() << endl;
                dup2(pipes[pipes.size()-1].fd[0], STDIN_FILENO); 
                close(pipes[pipes.size()-1].fd[0]);
                //cout << "case 1 0\n";
            }
        }else if(cmd.previosOP != 0 && cmd.nextOP != 0){
            if(cmd.nextOP != 2){
                if(cmd.nextOP == 3 || cmd.nextOP == 4){ // ex. ls | 'cat' |2 
                    int index = matchNumberPipeQueue(left);
                    if(index != -1){
                        dup2(pipes[pipes.size()-1].fd[0], STDIN_FILENO);
                        close(pipes[pipes.size()-1].fd[0]);
                        dup2(numberPipes[index].fd[1], STDOUT_FILENO);
                        if(cmd.nextOP == 4) dup2(numberPipes[index].fd[1], STDERR_FILENO);
                        close(numberPipes[index].fd[1]);
                        close(numberPipes[index].fd[0]);
                    }   
                } else if(cmd.previosOP==1 && cmd.nextOP == 5){ // ls | cat >2
                    if(cmd.userPipeErrorType==0){
                        int userPipeIndex = getUserPipeIndex(users[userIndex].ID ,cmd.readUserID);
                        //cout << "\nuserPipeIndex" << userPipeIndex << "\n";
                        dup2(userPipes[userPipeIndex].fd[1], STDOUT_FILENO);
                        close(userPipes[userPipeIndex].fd[0]);
                        close(userPipes[userPipeIndex].fd[1]);
                    } else {
                        dup2(FD_NULL, STDOUT_FILENO);
                    }
                    dup2(pipes[pipes.size()-1].fd[0], STDIN_FILENO);
                    close(pipes[pipes.size()-1].fd[0]);
                } else{ // ex. ls | 'cat' | cat
                    dup2(pipes[pipes.size()-2].fd[0], STDIN_FILENO);
                    close(pipes[pipes.size()-2].fd[0]);
                    dup2(pipes[pipes.size()-1].fd[1], STDOUT_FILENO);
                    close(pipes[pipes.size()-1].fd[1]);
                    close(pipes[pipes.size()-1].fd[0]);  
                }
            } else { // ex. ls | 'cat' > a.html
                dup2(pipes[pipes.size()-1].fd[0], STDIN_FILENO);
                close(pipes[pipes.size()-1].fd[0]);
                int filefd = open(cmd.redirectFileName.c_str(), O_TRUNC | O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
                dup2(filefd, STDOUT_FILENO);
                close(filefd);
            }
        }
        
        int a = 0;
        char **argv = cmd.buildArgv();
        a = execvp(cmd.currentCommand.c_str(), argv);
        if(a==-1) cerr << "Unknown command: [" << cmd.currentCommand << "]." << endl;
        exit(0);
    } else { // parent process
        ++processNum; // add
        if(pipes.size()!=0) {
            if(cmd.previosOP == 0 && cmd.nextOP != 0) { // ex. 'ls' | cat   (ls |2 or ls > a.html didn't need to care pipe)
                if(cmd.nextOP == 1){
                    close(pipes[pipes.size()-1].fd[1]);
                }
            } else if(cmd.previosOP != 0 && cmd.nextOP == 0){ // ex. ls | 'cat'
                close(pipes[pipes.size()-1].fd[0]);
            } else if(cmd.previosOP != 0 && cmd.nextOP != 0){ 
                if(cmd.nextOP == 1){ // ls | 'cat' | cat or 
                    if(cmd.previosOP == 6){ // cat <1 | cat 
                        if(cmd.userPipeErrorType==0){ 
                            int userPipeIndex = getUserPipeIndex(cmd.writeUserID ,users[userIndex].ID);
                            close(userPipes[userPipeIndex].fd[0]);
                            userPipes.erase(userPipes.begin()+userPipeIndex);
                        }
                        close(pipes[pipes.size()-1].fd[1]);
                    }else{
                        close(pipes[pipes.size()-2].fd[0]);
                        close(pipes[pipes.size()-1].fd[1]);
                    }
                } else if(cmd.previosOP == 1 && cmd.nextOP == 5){ // ls | cat >1
                    if(cmd.userPipeErrorType==0){
                        int userPipeIndex = getUserPipeIndex(users[userIndex].ID ,cmd.readUserID);
                        close(userPipes[userPipeIndex].fd[1]);
                    }
                    close(pipes[pipes.size()-1].fd[0]);
                } else { // ls | 'cat' |2 or ls | 'cat' > a.html
                    close(pipes[pipes.size()-1].fd[0]);
                }
            }
        } else {
            if(cmd.previosOP == 0 && cmd.nextOP == 5){ // ls >1
                if(cmd.userPipeErrorType==0){
                    int userPipeIndex = getUserPipeIndex(users[userIndex].ID ,cmd.readUserID);
                    close(userPipes[userPipeIndex].fd[1]);
                }
            } else if(cmd.previosOP == 6 && cmd.nextOP == 0){ // cat <1
                if(cmd.userPipeErrorType==0){
                    int userPipeIndex = getUserPipeIndex(cmd.writeUserID ,users[userIndex].ID);
                    close(userPipes[userPipeIndex].fd[0]);
                    userPipes.erase(userPipes.begin()+userPipeIndex);
                }
            } else if(cmd.previosOP == 6 && cmd.nextOP == 5){ // ls >1 <1
                if(cmd.userPipeErrorType==0){
                    int senderPipeIndex = getUserPipeIndex(users[userIndex].ID ,cmd.readUserID);
                    int receiverPipeIndex = getUserPipeIndex( cmd.writeUserID, users[userIndex].ID);
                    close(userPipes[senderPipeIndex].fd[1]);
                    close(userPipes[receiverPipeIndex].fd[0]);
                    userPipes.erase(userPipes.begin()+receiverPipeIndex);
                }else if(cmd.userPipeErrorType==2){ // receiver error
                    int senderPipeIndex = getUserPipeIndex(users[userIndex].ID ,cmd.readUserID);
                    close(userPipes[senderPipeIndex].fd[1]);
                }else if(cmd.userPipeErrorType==3){ // sender error
                    int receiverPipeIndex = getUserPipeIndex( cmd.writeUserID, users[userIndex].ID);
                    close(userPipes[receiverPipeIndex].fd[0]);
                    userPipes.erase(userPipes.begin()+receiverPipeIndex);
                }
            }
        }

        if(cmd.previosOP == 6 && (cmd.nextOP >= 1 && cmd.nextOP <=4)){
            cmd.previosOP = 0;
        }

        for(int i=0;i<numberPipes.size();++i){ // close all numberpipe when left = 0
            if(numberPipes[i].numberleft == 0){
                close(numberPipes[i].fd[1]);
                close(numberPipes[i].fd[0]); 
            }
        }

        int status = 0;
        //usleep(20000); 
        if(cmd.nextOP == 1 || cmd.nextOP == 3 || cmd.nextOP == 4 || cmd.nextOP == 5 || cmd.nextOP == 6){ // | |2 !2 don't hang on forever
            //waitpid(-1,&status,WNOHANG);
            if(waitpid(-1,&status,WNOHANG)>0){ // add
                --processNum; // add
            } // add
        } else {
            waitpid(pid,&status, 0); // > hang on if it didn't finish 
            --processNum; // add
        }
    }
}

void processToken(command &cmd, int &userIndex){

    // 記得把 parent process pipe fd要關掉
    int i = 0, left = 0;
    //cout << cmd.wholecommand << endl;
    for(;i<cmd.tokens.size();++i) {
        if(cmd.currentCommand == "") cmd.currentCommand = cmd.tokens[i]; // push cmd
        else if(!cmd.isOPToken(cmd.tokens[i])) cmd.commandArgument.push_back(cmd.tokens[i]); // cmd argument
        
        if(cmd.isOPToken(cmd.tokens[i]) || i == cmd.tokens.size()-1){ // if is optoken or last index
            cmd.previosOP = cmd.nextOP; // switch previous op and next op
            if(i==cmd.tokens.size() - 1 ){ // ls | 'ls' or ls | ls '|2'
                cmd.nextOP = 0;
                if(cmd.isNumberPipe(cmd.tokens[i])){
                    cmd.setNextOP(cmd.tokens[i]);
                    left = stoi(cmd.tokens[i].substr(1));
                    int inPipeQueue = matchNumberPipeQueue(left);
                    if(inPipeQueue == -1){ // didn't find same left numberpipe
                        numberPipes.push_back(pipestruct{});
                        if(pipe(numberPipes[numberPipes.size()-1].fd)<0){
                            cerr << "pipe error!" << endl;
                        }
                        numberPipes[numberPipes.size()-1].pipetype = cmd.nextOP;
                        numberPipes[numberPipes.size()-1].numberleft = left;
                    }
                } else if(cmd.isUserPipe(cmd.tokens[i])){
                    if(cmd.tokens[i][0]=='<'){ // cat <2 // 6,0
                        cmd.writeUserID = stoi(cmd.tokens[i].substr(1));
                        cmd.previosOP = 6;
                        string message;
                        int senderIndex = getUserIndexFromID(cmd.writeUserID);
                        if(senderIndex == -1){
                            cmd.userPipeErrorType = 1;
                            message = "*** Error: user #" + to_string(cmd.writeUserID) + " does not exist yet. ***\n";
                            if(write(users[userIndex].ssock, message.c_str(), message.size())<0){
                                cerr << "write error:" << strerror(errno) <<"\n";
                            }
                        } else {
                            if(existUserPipe(cmd.writeUserID, users[userIndex].ID)){
                                message = "*** " + users[userIndex].name + " (#" 
                                    + to_string(users[userIndex].ID) + ") just received from " 
                                    + users[senderIndex].name + " (#" 
                                    + to_string(users[senderIndex].ID) + ") by '" 
                                    + cmd.wholecommand +"' ***\n";
                                broadcast(message);
                            }else{
                                message = "*** Error: the pipe #" + to_string(cmd.writeUserID) 
                                        + "->#" + to_string(users[userIndex].ID) + " does not exist yet. ***\n";
                                cmd.userPipeErrorType = 1;
                                if(write(users[userIndex].ssock, message.c_str(), message.size())<0){
                                    cerr << "write error:" << strerror(errno) <<"\n";
                                }
                            }
                        }

                    } else if(cmd.tokens[i][0]=='>'){ // cat >2 or ls | cat >2 // 0,5 or 1,5
                        cmd.readUserID = stoi(cmd.tokens[i].substr(1));
                        cmd.nextOP = 5;
                        userpipestruct userPipe;
                        userPipe.senderUserID = users[userIndex].ID;
                        userPipe.receiverUserID = stoi(cmd.tokens[i].substr(1));
                        string message;
                        int receiverIndex = getUserIndexFromID(cmd.readUserID);
                        if(receiverIndex == -1){
                            cmd.userPipeErrorType = 1;
                            message = "*** Error: user #" + to_string(cmd.readUserID) + " does not exist yet. ***\n";
                            if(write(users[userIndex].ssock, message.c_str(), message.size())<0){
                                cerr << "write error:" << strerror(errno) <<"\n";
                            }
                        } else {
                            if(existUserPipe(userPipe.senderUserID, userPipe.receiverUserID)){
                                message = "*** Error: the pipe #" + to_string(users[userIndex].ID) 
                                        + "->#" + to_string(cmd.readUserID) + " already exists. ***\n";
                                cmd.userPipeErrorType = 1;
                                if(write(users[userIndex].ssock, message.c_str(), message.size())<0){
                                    cerr << "write error:" << strerror(errno) <<"\n";
                                }
                            } else {
                                message = "*** " + users[userIndex].name + " (#" 
                                        + to_string(users[userIndex].ID) + ") just piped '" 
                                        + cmd.wholecommand + "' to " + users[receiverIndex].name + " (#" 
                                        + to_string(users[receiverIndex].ID) + ") ***\n";
                                broadcast(message);
                                if(pipe(userPipe.fd)<0){
                                    cerr << "create pipe error:" << strerror(errno) <<"\n"; 
                                }
                                userPipes.push_back(userPipe);
                            }
                        }
                    }
                }
            } 
            else { // ls '|' ls or ls '>' a.html
                cmd.setNextOP(cmd.tokens[i]);
                if(cmd.nextOP == 1){
                    pipes.push_back(pipestruct{});
                    if(pipe(pipes[pipes.size()-1].fd) < 0) {
                        cerr << "create pipe fail" << endl;
                    }
                }else if(cmd.isUserPipe(cmd.tokens[i]) && i+1 < cmd.tokens.size()){
                    if(cmd.isUserPipe(cmd.tokens[i+1])){ // cat <2 >1 or cat >1 <2
                        cmd.previosOP = 6;
                        cmd.nextOP = 5;
                        if(cmd.tokens[i][0] == '<'){ // cat <2 >1
                            cmd.writeUserID = stoi(cmd.tokens[i].substr(1));
                            cmd.readUserID = stoi(cmd.tokens[i+1].substr(1));
                        } else { // cat >1 <2                                
                            cmd.readUserID = stoi(cmd.tokens[i].substr(1));
                            cmd.writeUserID = stoi(cmd.tokens[i+1].substr(1));
                        }

                        string message;
                        // receiver first
                        int senderIndex = getUserIndexFromID(cmd.writeUserID);
                        if(senderIndex == -1){
                            cmd.userPipeErrorType = 2;
                            message = "*** Error: user #" + to_string(cmd.writeUserID) + " does not exist yet. ***\n";
                            if(write(users[userIndex].ssock, message.c_str(), message.size())<0){
                                cerr << "write error:" << strerror(errno) <<"\n";
                            }
                        } else {
                            if(existUserPipe(cmd.writeUserID, users[userIndex].ID)){
                                message = "*** " + users[userIndex].name + " (#" 
                                    + to_string(users[userIndex].ID) + ") just received from " 
                                    + users[senderIndex].name + " (#" 
                                    + to_string(users[senderIndex].ID) + ") by '" 
                                    + cmd.wholecommand +"' ***\n";
                                broadcast(message);
                            }else{
                                message = "*** Error: the pipe #" + to_string(cmd.writeUserID) 
                                        + "->#" + to_string(users[userIndex].ID) + " does not exist yet. ***\n";
                                cmd.userPipeErrorType = 2;
                                if(write(users[userIndex].ssock, message.c_str(), message.size())<0){
                                    cerr << "write error:" << strerror(errno) <<"\n";
                                }
                            }
                        }

                        // sender second
                        userpipestruct userPipe;
                        userPipe.senderUserID = users[userIndex].ID;
                        userPipe.receiverUserID = cmd.readUserID;
                        int receiverIndex = getUserIndexFromID(cmd.readUserID);
                        if(receiverIndex == -1){
                            cmd.userPipeErrorType = cmd.userPipeErrorType == 2 ? 4 : 3;
                            message = "*** Error: user #" + to_string(cmd.readUserID) + " does not exist yet. ***\n";
                            if(write(users[userIndex].ssock, message.c_str(), message.size())<0){
                                cerr << "write error:" << strerror(errno) <<"\n";
                            }
                        } else {
                            if(existUserPipe(userPipe.senderUserID, userPipe.receiverUserID)){
                                message = "*** Error: the pipe #" + to_string(users[userIndex].ID) 
                                        + "->#" + to_string(cmd.readUserID) + " already exists. ***\n";
                                cmd.userPipeErrorType = cmd.userPipeErrorType == 2 ? 4 : 3;
                                if(write(users[userIndex].ssock, message.c_str(), message.size())<0){
                                    cerr << "write error:" << strerror(errno) <<"\n";
                                }
                            } else {
                                message = "*** " + users[userIndex].name + " (#" 
                                        + to_string(users[userIndex].ID) + ") just piped '" 
                                        + cmd.wholecommand + "' to " + users[receiverIndex].name + " (#" 
                                        + to_string(users[receiverIndex].ID) + ") ***\n";
                                broadcast(message);
                                if(pipe(userPipe.fd)<0){
                                    cerr << "create pipe error:" << strerror(errno) <<"\n"; 
                                }
                                userPipes.push_back(userPipe);
                            }
                        }
                    }else if(cmd.isNormalPipe(cmd.tokens[i+1])){ // cat <2 |(|1, >)
                        cmd.writeUserID = stoi(cmd.tokens[i].substr(1));
                        cmd.previosOP = 6;
                        cmd.setNextOP(cmd.tokens[i+1]);
                        string message;
                        int senderIndex = getUserIndexFromID(cmd.writeUserID);
                        if(cmd.nextOP == 2 && i+2 < cmd.tokens.size()){
                            cmd.redirectFileName = cmd.tokens[i+2];
                            cmd.tokens.pop_back();
                        }else if(cmd.nextOP == 1){
                            pipes.push_back(pipestruct{});
                            if(pipe(pipes[pipes.size()-1].fd) < 0) {
                                cerr << "create pipe fail" << endl;
                            }
                        } else if(cmd.nextOP==3||cmd.nextOP==4){
                            left = stoi(cmd.tokens[i+1].substr(1));
                            int inPipeQueue = matchNumberPipeQueue(left);
                            if(inPipeQueue == -1){ // didn't find same left numberpipe
                                numberPipes.push_back(pipestruct{});
                                if(pipe(numberPipes[numberPipes.size()-1].fd)<0){
                                    cerr << "pipe error!" << endl;
                                }
                                numberPipes[numberPipes.size()-1].pipetype = cmd.nextOP;
                                numberPipes[numberPipes.size()-1].numberleft = left;
                            }
                        }
                        if(senderIndex == -1){
                            cmd.userPipeErrorType = 1;
                            message = "*** Error: user #" + to_string(cmd.writeUserID) + " does not exist yet. ***\n";
                            if(write(users[userIndex].ssock, message.c_str(), message.size())<0){
                                cerr << "write error:" << strerror(errno) <<"\n";
                            }
                        } else {
                            if(existUserPipe(cmd.writeUserID, users[userIndex].ID)){
                                message = "*** " + users[userIndex].name + " (#" 
                                    + to_string(users[userIndex].ID) + ") just received from " 
                                    + users[senderIndex].name + " (#" 
                                    + to_string(users[senderIndex].ID) + ") by '" 
                                    + cmd.wholecommand +"' ***\n";
                                broadcast(message);
                            }else{
                                message = "*** Error: the pipe #" + to_string(cmd.writeUserID) 
                                        + "->#" + to_string(users[userIndex].ID) + " does not exist yet. ***\n";
                                cmd.userPipeErrorType = 1;
                                if(write(users[userIndex].ssock, message.c_str(), message.size())<0){
                                    cerr << "write error:" << strerror(errno) <<"\n";
                                }
                            }
                        }
                    }
                    ++i;  
                }else{
                    if(cmd.nextOP == 2 && i+1 < cmd.tokens.size()){
                        cmd.redirectFileName = cmd.tokens[i+1];
                        cmd.tokens.pop_back();
                    }
                }
            }
            //cout << cmd.previosOP << " --- " << cmd.nextOP << endl;
            forkandexec(cmd, left, userIndex);
            cmd.readUserID = -1;
            cmd.writeUserID = -1;
            cmd.userPipeErrorType = 0;
            cmd.currentCommand = "";
            cmd.commandArgument.clear();
            for(int i = 0;i<numberPipes.size();++i){
                //cout << "Left:" << numberPipes[i].numberleft << "\n";
                if(numberPipes[i].numberleft <= 0){
                    
                    numberPipes.erase(numberPipes.begin()+i);
                    --i;
                } 
            }
        } 
    }
    pipes.clear();
}

int processCommand(command &cmd, int &userIndex){
    if(isBuildinCmd(cmd)){
        cmd.commandType = 1;
        if(cmd.tokens[0] == "setenv"){

            if(cmd.tokens.size() < 3){
                cerr << "loss parameters" << endl;
                return 0;
            }
            setenv(cmd.tokens[1].c_str(), cmd.tokens[2].c_str(), 1);
            users[userIndex].env[cmd.tokens[1]] = cmd.tokens[2];
            decreaseNumberPipeLeft();
            dup2recovery();
        } else if(cmd.tokens[0] == "printenv"){
            if(cmd.tokens.size()<2){
                cerr << "loss parameters" << endl;
                return 0;
            } 
            
            if(getenv(cmd.tokens[1].c_str())){
                cout << getenv(cmd.tokens[1].c_str()) << endl;
            }
            decreaseNumberPipeLeft();
            dup2recovery();
        } else if(cmd.tokens[0] == "exit"){
            //cout << "before recovery" << endl;
            idused[users[userIndex].ID-1] = false;
            logout(userIndex);
            dup2recovery();
            //cout << "after recovery" << endl;
            FD_CLR(users[userIndex].ssock, &afds);
            removeUserPipe(users[userIndex].ID);
            decreaseNumberPipeLeft();
            for(int i=0;i<numberPipes.size();++i){
                close(numberPipes[i].fd[0]);
            }
            close(users[userIndex].ssock);
            return -1;
        } else if(cmd.tokens[0] == "who"){
            who(userIndex);
            decreaseNumberPipeLeft();
            dup2recovery();
        } else if(cmd.tokens[0] == "tell"){
            string message = "";
            
            for(int i=2;i<cmd.tokens.size();++i){
                if(i!=cmd.tokens.size()-1) message += cmd.tokens[i]+" ";
                else message += cmd.tokens[i];
            }
            unicast(userIndex, stoi(cmd.tokens[1]), message);
            decreaseNumberPipeLeft();
            dup2recovery();
        } else if(cmd.tokens[0] == "yell"){
            string message = "*** " + users[userIndex].name + " yelled ***: ";
            for(int i=1;i<cmd.tokens.size();++i){
                if(i!=cmd.tokens.size()-1) message += cmd.tokens[i]+" ";
                else message += cmd.tokens[i];
            } 
            message += "\n";
            broadcast(message);
            decreaseNumberPipeLeft();
            dup2recovery();
        } else if(cmd.tokens[0] == "name"){
            string message = "";
            for(int i=1;i<cmd.tokens.size();++i){
                if(i!=cmd.tokens.size()-1) message += cmd.tokens[i]+" ";
                else message += cmd.tokens[i];
            }
            name(userIndex, message);
            decreaseNumberPipeLeft();
            dup2recovery();
        } else {
            dup2recovery();
        }
        
    } else {
        cmd.commandType = 2;
        processToken(cmd, userIndex);
        decreaseNumberPipeLeft();
        dup2recovery();
    }
    return 0;
}

int executable(int &userIndex, string &inputCmd){

    stringstream ss;
    //cout << "iputCmd : " << inputCmd << " Size : " << inputCmd.size() << endl;
    //while(cout << "% " && getline(cin, cmdLine)){
    command currentcmd;
    currentcmd.wholecommand = inputCmd;
    if(currentcmd.wholecommand[currentcmd.wholecommand.size()-1] == '\n' || currentcmd.wholecommand[currentcmd.wholecommand.size()-1] == '\r') currentcmd.wholecommand.pop_back();
    if(currentcmd.wholecommand[currentcmd.wholecommand.size()-1] == '\n' || currentcmd.wholecommand[currentcmd.wholecommand.size()-1] == '\r') currentcmd.wholecommand.pop_back();
    ss << inputCmd;
    string token;
    vector<string> tmp;
    
    while(ss>>token){
        tmp.push_back(token);
    }
    if(tmp.size()==0){
        dup2recovery();
        return 1;
    }
    for(int i=0;i<tmp.size();++i){
        currentcmd.tokens.push_back(tmp[i]);
        if(currentcmd.isNumberPipe(tmp[i]) || i == tmp.size() - 1){
            int state = processCommand(currentcmd, userIndex);
            if(state == -1) return state;
            currentcmd = command{};
        }
    }
    ss.clear();
    return 1;
    //}
}

void rwgserver(){
    
    int msock;
    if((msock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        cerr << "Create Main Socket fail" << strerror(errno) << endl;;
        exit(0);
    }
    
    const int enable = 1;
    if(setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int))<0){
        cerr << "set socket option error:" << strerror(errno) << endl;
    }
    sockaddr_in serverAddr;
    bzero((char *)&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(serverport);

    if(bind(msock, (const sockaddr *)&serverAddr, sizeof(serverAddr))<0){
        cerr << "Bind socker fail:" << strerror(errno) << endl;
        close(msock);
        exit(0);
    }
    
    if(listen(msock, 30)<0){
        cerr << "socket " << msock << "listen failed" << strerror(errno) << endl;
        exit(0);
    }
    setenv("PATH" , "bin:.", 1);

    int nfds = getdtablesize();
    int currentfd;
    char buffer[15000] = {};
    FD_ZERO(&afds);
    FD_ZERO(&rfds);
    FD_SET(msock, &afds);
    for(int i=0;environ[i]!=NULL;++i){
        string key(environ[i]);
        auto it = key.find('=');
        backupenv[key.substr(0, it)] = key.substr(it+1, key.size());
    }

    while(1){
        memcpy(&rfds, &afds, sizeof(rfds));
        timeval tv = {0, 10};
        /*if(select(maxfd+1, &rfds, NULL, NULL, &tv) < 0){
            cerr << "select fail:" << strerror(errno) << "\n";
            continue;
        }*/
        for(int i=0;i<users.size();++i){
            if(users[i].ssock>maxfd){
                maxfd = users[i].ssock;
            }
        }
        if(select(maxfd+1, &rfds, NULL, NULL, (struct timeval *)0) < 0){
            cerr << "select fail:" << strerror(errno) << "\n";
            continue;
        }

        if(FD_ISSET(msock, &rfds)){
            
            int ssock;
            sockaddr_in clientAddr;
            bzero((char *)&clientAddr, sizeof(clientAddr));
            unsigned int alen; 
            alen = sizeof(clientAddr);
            ssock = accept(msock, (struct sockaddr *)&clientAddr, &alen);
            if(ssock < 0){
                cerr << "accpet error:" << strerror(errno) << "\n";
            }
            userinfo user = {};
            int i=0;
            for(i = 0;i<30;++i){
                if(idused[i] == false){
                    idused[i] = true;
                    user.ID = i+1;
                    break;
                }
            }
            if(i==30) continue;
            
            user.ssock = ssock;
            user.port = ntohs(clientAddr.sin_port);
            user.addr = inet_ntoa(clientAddr.sin_addr);
            user.name = "(no name)";
            
            cout << "ID:" << user.ID <<"\nssock:" << user.ssock << "\nport:" << user.port << "\naddr:" << user.addr <<"\n";
            cout << "===========================" << endl;
            users.push_back(user);
            std::sort(users.begin(), users.end(), compareByID);
            welcome(user.ID);
            if(write(user.ssock, "% ", 2)<0){
                cerr << "write error:" << strerror(errno) <<"\n";
            }
            FD_SET(ssock, &afds);
        }

        for(int i=0;i<users.size();++i){
            currentfd = users[i].ssock;
            //cout << "before accept read\n";
            if(FD_ISSET(currentfd, &rfds)){
                //cout << "accept read\n";
                memset(buffer, '\0', 15000);
                int n = read(currentfd, buffer, 15000);
                if(n<0){
                    cerr << "read error:" << strerror(errno) << "\n";
                    continue;
                }
                //cout << buffer;
                stdinfdTmp = dup(STDIN_FILENO), stdoutTmp = dup(STDOUT_FILENO), stderrTmp = dup(STDERR_FILENO);
                //cout << "Try1" << endl;
                string tmpS(buffer);
                setUserEnv(i);
                numberPipes = users[i].userNumberPipes;
                dup2toclient(i);
                int state = executable(i, tmpS);
                users[i].userNumberPipes = numberPipes;
                numberPipes.clear();
                //cout <<"state: " << state << endl;
                if(state == -1){
                    users[i].userNumberPipes.clear();
                    users.erase(users.begin()+i);
                    //cout << users.size() << endl;
                    break;
                }else {
                    if(write(currentfd, "% ", 2)<0){
                        cerr << "write error:" << strerror(errno) <<"\n";
                    }
                }

            }
        }
    }
}

int main(int argc, char *argv[]){
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    signal(SIGCHLD, signal_child);
    signal(SIGINT, signal_terminate);
    for(int i=0;i<30;++i) idused[i] = false;
    if(argc < 2) {
        cerr << "No port input\n";
        exit(0);
    }
    
    serverport = stoi(argv[1]);
    processNum = 0;
    rwgserver();
    
    return 0;
}