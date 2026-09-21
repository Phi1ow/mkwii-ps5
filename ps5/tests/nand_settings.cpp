#include "nand_settings_contract.h"
#include <iostream>
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected a fresh test root");
    std::cout<<"PASS "<<test_nand_settings_contract(std::filesystem::path(argv[1]))<<" NAND publication checks, actual PS5 branch on host\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
