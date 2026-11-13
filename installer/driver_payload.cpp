#include "driver_payload.hpp"
#include <windows.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,nullptr,0,nullptr,nullptr);
    if (size<=1) return {};
    std::string out(static_cast<std::size_t>(size-1),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,out.data(),size,nullptr,nullptr);
    return out;
}
std::wstring service_image_path(const wchar_t* service) {
    const std::wstring key=L"SYSTEM\\CurrentControlSet\\Services\\"+std::wstring(service);
    HKEY handle=nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,key.c_str(),0,KEY_READ,&handle)!=ERROR_SUCCESS)
        throw std::runtime_error("Driver service "+narrow(service)+" is not registered");
    wchar_t value[32768]{};
    DWORD bytes=sizeof(value),type=0;
    const auto status=RegQueryValueExW(handle,L"ImagePath",nullptr,&type,reinterpret_cast<BYTE*>(value),&bytes);
    RegCloseKey(handle);
    if (status!=ERROR_SUCCESS) throw std::runtime_error("Driver service "+narrow(service)+" has no image path");
    if (type!=REG_EXPAND_SZ&&type!=REG_SZ) throw std::runtime_error("Unexpected driver image path type");
    return value;
}
std::wstring expand(const std::wstring& path) {
    const std::wstring prefix=L"\\SystemRoot\\";
    if (path.rfind(prefix,0)==0) return L"C:\\Windows"+path.substr(prefix.size()-1);
    if (path.rfind(L"\\??\\",0)==0) return path.substr(4);
    if (path.rfind(L"System32\\",0)==0) return L"C:\\Windows\\"+path;
    return path;
}
std::vector<char> read_file(const std::wstring& path) {
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("Driver image "+narrow(path)+" cannot be read");
    return std::vector<char>((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
}
}
namespace lhdc {
void verify_driver_payload(const std::filesystem::path& staged_sys,const wchar_t* service) {
    const auto image=expand(service_image_path(service));
    if (read_file(image)==read_file(staged_sys.wstring())) return;
    throw std::runtime_error("Installed driver image "+narrow(image)+
        " differs from the staged payload; the driver package was published earlier with the same INF, "
        "so the rebuilt driver was not installed. Bump the INF DriverVer and deploy again.");
}
}
