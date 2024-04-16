#include<iostream>
#include<string>
#include<string.h>
#include<sstream>
#include<unistd.h>
#include<signal.h>
#include<vector>
#include<sys/wait.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
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

vector<pipestruct> pipes;
vector<pipestruct> numberPipes;
int maxProcessNum = 150;
int processNum = 0;
int cpid = 0;

void signal_child(int signal){
    if(signal != SIGCHLD) return;
	int status;
	//while(waitpid(-1,&status,WNOHANG) > 0){}
    while(waitpid(-1,&status,WNOHANG) > 0){ --processNum; } // add
}

void signal_quit(int signal){
    if(signal != SIGQUIT) return;
    //kill(0, SIGQUIT);
    while(waitpid(-1,NULL,WNOHANG) > 0);
    exit(0);
}

void signal_terminate(int signal){
    if(signal != SIGINT) return;
    kill(cpid, SIGQUIT);
    waitpid(cpid, NULL, 0);
    exit(0);
}

bool isBuildinCmd(command currentcmd){
    return currentcmd.tokens[0] == "setenv" || currentcmd.tokens[0] == "printenv" || currentcmd.tokens[0] == "exit";
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

void processCommand(command &cmd, int &ssock){
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
            close(ssock);
            exit(0);
        }
        decreaseNumberPipeLeft();
    } else {
        cmd.commandType = 2;
        processToken(cmd);
        decreaseNumberPipeLeft();
    }
}

void executable(int &ssock){
    
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
                processCommand(currentcmd, ssock);
                currentcmd = command{};
            }
        }

        ss.clear();
    }
}

int main(int argc, char *argv[]){

    signal(SIGCHLD, signal_child);
    signal(SIGQUIT, signal_quit);
    signal(SIGINT, signal_terminate);
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    if(argc < 2) {
        cerr << "No port input\n";
        exit(0);
    }

    processNum = 0;

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
    serverAddr.sin_port = htons(stoi(argv[1]));

    if(bind(msock, (const sockaddr *)&serverAddr, sizeof(serverAddr))<0){
        cerr << "Bind socker fail:" << strerror(errno) << endl;
        close(msock);
        exit(0);
    }
    
    if(listen(msock, 1)<0){
        cerr << "socket " << msock << "listen failed:" << strerror(errno) << endl;
        exit(0);
    }

    setenv("PATH" , "bin:.", 1);
    while(1){
        sockaddr_in clientAddr = {};
        bzero((char *)&clientAddr, sizeof(clientAddr));
        unsigned int client_len = sizeof(clientAddr);
        int ssock = accept(msock, (sockaddr *)&clientAddr, &client_len);
        
        switch (cpid = fork())
        {
        case 0: // child process
            dup2(ssock, STDIN_FILENO);
            dup2(ssock, STDOUT_FILENO);
            dup2(ssock, STDERR_FILENO);
            close(msock);
            executable(ssock);
            break;
        case -1: // fork error
            cerr << "fork error:\n" << strerror(errno) << endl;
            break;
        default: // parent process
            close(ssock);
            waitpid(cpid, NULL, 0);
            break;
        }
    }

    close(msock);

    return 0;
}