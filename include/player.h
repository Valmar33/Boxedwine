#ifndef __PLAYER_H__
#define __PLAYER_H__

#ifdef BOXEDWINE_RECORDER
class Player {
public:
    static bool start(std::string directory);
    static Player* instance;

    void initCommandLine(std::string root, std::string zip, std::string working, const char **argv, U32 argc);
    void runSlice();
    void screenChanged();

    FILE* file;
    std::string directory;
    std::string version;
    U64 lastCommandTime;
    std::string nextCommand;
private:    
    std::string nextValue;
    void readCommand();
};
#endif

#endif