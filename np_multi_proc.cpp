#include<iostream>
#include<string>
#include<stdio.h>
#include<string.h>
#include<sstream>
#include<unistd.h>
#include<signal.h>
#include<vector>
#include<sys/wait.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<map>
#include<arpa/inet.h>
#include<dirent.h>
#include<sys/shm.h>
#include<sys/mman.h>
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
*/

class command
{
public:
    int commandType;
    int previosOP;
    int nextOP;
    string redirectFileName;
    string currentCommand;
    vector<string> commandArgument;
    vector<string> tokens;

    bool isNumberPipe(string token);
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
    commandType = 0;
    nextOP = 0;
    previosOP = 0;
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

bool command::isOPToken(string token){
    if(token == "|" || token == ">"){
        return true;
    }else if(token.size() >= 2 &&
     (token[0] == '|' || token[0] == '!') &&
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

struct userinfo
{
    bool alive;
    int ID;
    int ssock;
    int port;
    pid_t cpid;
    char name[20];
    char addr[INET_ADDRSTRLEN];
};

vector<pipestruct> pipes;
vector<pipestruct> numberPipes;
userinfo currentUser = {};
bool idUsed[30] = {false};
int maxProcessNum = 500;
int processNum = 0;
int serverPort, shmUserInfos, currentIndex, shmMessage;
string tmpMessage;
userinfo *userInfoPtr;
char* messagePtr;

void signal_child(int signal){
    if(signal != SIGCHLD) return;
	int status;
	//while(waitpid(-1,&status,WNOHANG) > 0){}
    while(waitpid(-1,&status,WNOHANG) > 0){ --processNum; } // add
}

void signal_terminate(int signal){
    if(signal != SIGINT) return;
    munmap(messagePtr, 15000);
	munmap(userInfoPtr, 30 * sizeof(userinfo));
    shm_unlink("UserInfos");
    shm_unlink("Message");
    exit(0);
}

void signal_usr1(int signal){ // print message
    if(signal != SIGUSR1) return;
    cout << string(messagePtr);
}

bool isBuildinCmd(command currentcmd){
    return currentcmd.tokens[0] == "setenv" || currentcmd.tokens[0] == "printenv" 
        || currentcmd.tokens[0] == "exit" || currentcmd.tokens[0] == "who"
        || currentcmd.tokens[0] == "tell" || currentcmd.tokens[0] == "yell"
        || currentcmd.tokens[0] == "name";
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
        if(numberPipes[i].numberleft < 0){
            numberPipes.erase(numberPipes.begin()+i);
            --i;
        } 
    }
}

void broadcast(){
    for(int i=0; i<30;++i){
        if(userInfoPtr[i].alive){
            kill(userInfoPtr[i].cpid, SIGUSR1);
        } 
    }
}

void who(){
    cout << "<ID>    <nickname>  <IP:port>           <indicate me>\n";
    for(int i=0;i<30;++i){
        if(userInfoPtr[i].alive){
            cout << userInfoPtr[i].ID << "   " << userInfoPtr[i].name
                 << "   " << userInfoPtr[i].addr << ":" << userInfoPtr[i].port;
            if(currentIndex == i) cout <<"<-me";
            cout << '\n';
        }
    }
}

void name(string inputName){
    bool exist = false;
    for(int i=0 ;i<30;++i){
        if(userInfoPtr[i].alive && strcmp(userInfoPtr[i].name, inputName.c_str())==0){
            exist = true;
            break;
        }
    }
    if(exist){
        cout << "*** User '" << inputName <<"' already exists. ***\n";
    } else {
        strcpy(userInfoPtr[currentIndex].name, inputName.c_str());
        tmpMessage.clear();
        tmpMessage = "*** Use from " + string(userInfoPtr[currentIndex].addr) + ":" 
                    + to_string(userInfoPtr[currentIndex].port) + "is named '"
                    + string(userInfoPtr[currentIndex].name) + "'. ***\n";
        memset(messagePtr, '\0', 15000);
        strcpy(messagePtr, tmpMessage.c_str());
        broadcast();
    }
}

void unicast(int targetID){
    for(int i=0;i<30;++i){
        if(userInfoPtr[i].alive && currentIndex !=i && userInfoPtr[i].ID == targetID){
            kill(userInfoPtr[i].cpid, SIGUSR1);
        }
    }
}

void forkandexec(command &cmd, int left){
    RE:
    while(processNum >= maxProcessNum);
    int pid = fork();
    if(pid < 0) {
        
        int status = 0;
		while(waitpid(-1,&status,WNOHANG) > 0){ --processNum; }
        goto RE;
    }else if(pid == 0) { // chld process

        for(int i=0;i<numberPipes.size();++i){
            if(numberPipes[i].numberleft == 0){
                close(numberPipes[i].fd[1]);
                dup2(numberPipes[i].fd[0], STDIN_FILENO);
                close(numberPipes[i].fd[0]); 
            }
        }

        if(cmd.previosOP == 0 && cmd.nextOP != 0) { // ex. 'ls' | ls
            
            if( pipes.size() == 0 && (cmd.nextOP == 3 || cmd.nextOP == 4) && left != 0){ // 'ls' |2
                int index = matchNumberPipeQueue(left);
                if(index != -1){
                    dup2(numberPipes[index].fd[1], STDOUT_FILENO);
                    if(cmd.nextOP == 4) dup2(numberPipes[index].fd[1], STDERR_FILENO);
                    close(numberPipes[index].fd[1]);
                    close(numberPipes[index].fd[0]);
                }
            }else if(pipes.size() == 0 && cmd.nextOP == 2){ // 'ls' > a.html
                int filefd = open(cmd.redirectFileName.c_str(), O_TRUNC | O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
                dup2(filefd, STDOUT_FILENO);
                close(filefd);
            } else { // not appear previosOP == 0 , nextOP == 3(4) this case, because pipes.size() will == 0  ex. ls |
                // ex. 'ls' | cat
                dup2(pipes[pipes.size()-1].fd[1], STDOUT_FILENO);
                close(pipes[pipes.size()-1].fd[1]);
                close(pipes[pipes.size()-1].fd[0]);
            }

        } else if(cmd.previosOP != 0 && cmd.nextOP == 0){ // previosOP == 1 , nextOP == 0   ex. ls | 'ls'
            dup2(pipes[pipes.size()-1].fd[0], STDIN_FILENO); 
            close(pipes[pipes.size()-1].fd[0]);
        } else if(cmd.previosOP != 0 && cmd.nextOP != 0){
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
                }else{ // ex. ls | 'cat' | cat
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
                if(cmd.nextOP == 1){ // // ls | 'cat' | cat
                    close(pipes[pipes.size()-2].fd[0]);
                    close(pipes[pipes.size()-1].fd[1]);
                } else { // ls | 'cat' |2 or ls | 'cat' > a.html
                    close(pipes[pipes.size()-1].fd[0]);
                }
            }
        }

        for(int i=0;i<numberPipes.size();++i){ // close all numberpipe when left = 0
            if(numberPipes[i].numberleft == 0){
                close(numberPipes[i].fd[1]);
                close(numberPipes[i].fd[0]); 
            }
        }

        int status = 0;
        usleep(20000); 
        if(cmd.nextOP == 1 || cmd.nextOP == 3 || cmd.nextOP == 4){ // | |2 !2 don't hang on forever
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

void processToken(command &cmd){

    // 記得把 parent process pipe fd要關掉
    int i = 0, left = 0;
    
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
                }
            } 
            else { // ls '|' ls or ls '>' a.html
                cmd.setNextOP(cmd.tokens[i]);
                if(cmd.nextOP == 1){
                    pipes.push_back(pipestruct{});
                    if(pipe(pipes[pipes.size()-1].fd) < 0) {
                        cerr << "create pipe fail" << endl;
                    }
                }else{
                    if(cmd.nextOP == 2 && i+1 < cmd.tokens.size()){
                        cmd.redirectFileName = cmd.tokens[i+1];
                        cmd.tokens.pop_back();
                    }
                }
            }
            
            forkandexec(cmd, left);
            cmd.currentCommand = "";
            cmd.commandArgument.clear();
        } 
    }
    pipes.clear();
}

void processCommand(command &cmd){
    if(isBuildinCmd(cmd)){
        cmd.commandType = 1;
        if(cmd.tokens[0] == "setenv"){

            if(cmd.tokens.size() < 3){
                cerr << "loss parameters" << endl;
                return;
            } 
            setenv(cmd.tokens[1].c_str(), cmd.tokens[2].c_str(), 1);
        } else if(cmd.tokens[0] == "printenv"){
            if(cmd.tokens.size()<2){
                cerr << "loss parameters" << endl;
                return;
            } 
            
            if(getenv(cmd.tokens[1].c_str())){
                cout << getenv(cmd.tokens[1].c_str()) << endl;
            }
            
        } else if(cmd.tokens[0] == "exit"){
            userInfoPtr[currentIndex].alive = false;
            userInfoPtr[currentIndex].cpid = 0;
            memset(userInfoPtr[currentIndex].name, '\0', 20);
            strcpy(userInfoPtr[currentIndex].name, "(no name)");
            close(userInfoPtr[currentIndex].ssock);
            exit(0);
        } else if(cmd.tokens[0] == "tell"){
            tmpMessage.clear();
            for(int i=2;i<cmd.tokens.size();++i) tmpMessage += cmd.tokens[i];
            tmpMessage += '\n';
            memset(messagePtr, '\0', 15000);
            strcpy(messagePtr, tmpMessage.c_str());
            unicast(stoi(cmd.tokens[1]));
        } else if(cmd.tokens[0] == "yell"){
            tmpMessage.clear();
            for(int i=1;i<cmd.tokens.size();++i) tmpMessage += cmd.tokens[i];
            tmpMessage += "\n";
            memset(messagePtr, '\0', 15000);
            strcpy(messagePtr, tmpMessage.c_str());
            broadcast();
        } else if(cmd.tokens[0] == "who"){
            who();
        } else if(cmd.tokens[0] == "name"){
            string inputName;
            for(int i=1;i<cmd.tokens.size();++i) inputName += cmd.tokens[i];
            name(inputName);
        }
        decreaseNumberPipeLeft();
    } else {
        cmd.commandType = 2;
        processToken(cmd);
        decreaseNumberPipeLeft();
    }
}

void executable(){

    string cmdLine;
    stringstream ss;
    while(cout << "% " && getline(cin, cmdLine)){
        command currentcmd;
        
        ss << cmdLine;
        string token;
        vector<string> tmp;
        
        while(ss>>token){
            tmp.push_back(token);
        }
        for(int i=0;i<tmp.size();++i){
            currentcmd.tokens.push_back(tmp[i]);
            if(currentcmd.isNumberPipe(tmp[i]) || i == tmp.size() - 1){
                processCommand(currentcmd);
                currentcmd = command{};
            }
        }

        ss.clear();
    }
}

void rwgserver(){
    processNum = 0;
    signal(SIGCHLD, signal_child);
    signal(SIGUSR1, signal_usr1);
    int msock;
    if((msock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        cerr << "Create Main Socket fail:" << strerror(errno) << endl;;
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
    serverAddr.sin_port = htons(serverPort);

    if(bind(msock, (const sockaddr *)&serverAddr, sizeof(serverAddr))<0){
        cerr << "Bind socker fail:" << strerror(errno) << endl;
        close(msock);
        exit(0);
    }

    if(listen(msock, 30)<0){
        cerr << "socket " << msock << "listen failed:" << strerror(errno) << endl;
        exit(0);
    }
    setenv("PATH" , "bin:.", 1);

    shmUserInfos = shm_open("UserInfos", O_CREAT | O_RDWR, 0666);
    ftruncate(shmUserInfos, 30 * sizeof(userinfo));
    userInfoPtr = (userinfo *)mmap(NULL, 30 * sizeof(userinfo), PROT_READ | PROT_WRITE, MAP_SHARED, shmUserInfos, 0);
    for(int i=0;i<30;++i){
        userInfoPtr[i].alive = false;
        userInfoPtr[i].ID = 0;
        userInfoPtr[i].port = 0;
        userInfoPtr[i].ssock = 0;
        userInfoPtr[i].cpid = 0;
        memset(userInfoPtr[i].addr, '\0', sizeof(userInfoPtr[i].addr));
        memset(userInfoPtr[i].name, '\0', sizeof(userInfoPtr[i].name));
        strcpy(userInfoPtr[i].name, "(no name)");
    }

    shmMessage = shm_open("Message", O_CREAT | O_RDWR, 0666);
    ftruncate(shmMessage, 15000);
    messagePtr = (char *)mmap(NULL, 15000, PROT_READ | PROT_WRITE, MAP_SHARED, shmMessage, 0);
    memset(messagePtr, '\0', 15000);

    while(1){
        sockaddr_in clientAddr = {};
        bzero((char *)&clientAddr, sizeof(clientAddr));
        unsigned int client_len = sizeof(clientAddr);
        int ssock = accept(msock, (sockaddr *)&clientAddr, &client_len);
        
        for(int i=0;i<30;++i){
            if(!idUsed[i]){
                currentIndex = i;
                idUsed[i] = true;
                break;
            }
        }
        int child_pid;
        switch (child_pid = fork())
        {
        case 0: // child process
            dup2(ssock, STDIN_FILENO);
            dup2(ssock, STDOUT_FILENO);
            dup2(ssock, STDERR_FILENO);
            close(msock);
            userInfoPtr[currentIndex].ID = currentIndex+1;
            userInfoPtr[currentIndex].ssock = ssock;
            userInfoPtr[currentIndex].port = ntohs(clientAddr.sin_port);
            strcpy(userInfoPtr[currentIndex].addr, inet_ntoa(clientAddr.sin_addr));
            userInfoPtr[currentIndex].alive = true;
			cout << "****************************************\n";
			cout << "** Welcome to the information server. **\n";
			cout << "****************************************\n";
            tmpMessage = "*** User '" + string(userInfoPtr[currentIndex].name) + "' entered from " + string(userInfoPtr[currentIndex].addr) + ":" + to_string(userInfoPtr[currentIndex].port) + ". ***\n";"*** User '" + string(userInfoPtr[currentIndex].name) + "' entered from " + string(userInfoPtr[currentIndex].addr) + ":" + to_string(userInfoPtr[currentIndex].port) + ". ***\n";
            memset(messagePtr, '\0', 15000);
            strcpy(messagePtr, tmpMessage.c_str());
            broadcast();
            tmpMessage.clear();
            executable();
            
            close(ssock);
            break;
        case -1: // fork error
            cerr << "fork error:\n" << strerror(errno) << endl;
            break;
        default: // parent process
            userInfoPtr[currentIndex].cpid = child_pid;
            close(ssock);
            currentIndex = -1;
            //waitpid(child_pid, NULL, 0);
            break;
        }
    }
}

int main(int argc, char *argv[]){

    processNum = 0;
    currentIndex = -1;
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    signal(SIGCHLD, signal_child);
    signal(SIGINT, signal_terminate);
    signal(SIGUSR1, signal_usr1);

    if(argc < 2) {
        cerr << "No port input\n";
        exit(0);
    }
    serverPort = stoi(argv[1]);
    rwgserver();
    signal_terminate(SIGINT);
    
    return 0;
}