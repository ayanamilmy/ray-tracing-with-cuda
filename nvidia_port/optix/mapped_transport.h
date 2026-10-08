#pragma once
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <streambuf>
#include <stdexcept>
#include <string>
#include <cstdint>
namespace rt {
class MappedBuffer {
public:
    char *data=nullptr;
    size_t size=0;
    MappedBuffer(const std::string &path,size_t expected,bool exact) {
        int fd=open(path.c_str(),O_RDWR);
        if(fd<0) throw std::runtime_error("Cannot open mapped buffer");
        struct stat info{};
        if(fstat(fd,&info) || info.st_size<=0 || (exact?uint64_t(info.st_size)!=expected:uint64_t(info.st_size)<expected)) {
            close(fd);throw std::runtime_error("Invalid mapped buffer length");
        }
        size=size_t(info.st_size);
        void *address=mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        close(fd);
        if(address==MAP_FAILED) throw std::runtime_error("Cannot map shared buffer");
        data=static_cast<char*>(address);
    }
    MappedBuffer(const MappedBuffer&)=delete;
    ~MappedBuffer() {if(data) munmap(data,size);}
};
class MemoryInput : public std::streambuf {
public:
    MemoryInput(char *data,size_t size) {setg(data,data,data+size);}
};
class MemoryOutput : public std::streambuf {
public:
    MemoryOutput(char *data,size_t size) {setp(data,data+size);}
};
} // namespace rt
