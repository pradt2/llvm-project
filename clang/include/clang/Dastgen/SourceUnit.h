#ifndef __DASTGEN2_SourceUnit__
#define __DASTGEN2_SourceUnit__

#include <string>
#include <vector>

class SourceUnit {
public:
    std::string name;
    std::vector<std::string> path;
    std::string contents;
};

#endif
