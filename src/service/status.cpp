#include "status.hpp"
#include "avdtp/protocol.hpp"
#include "host/json.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include "control/rfcomm.hpp"
#include "transport/windows.hpp"
namespace lhdc {
bool adaptive_bitrate_enabled() {
    DWORD enabled=0,size=sizeof(enabled);
    const auto result=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"AdaptiveBitrate",RRF_RT_REG_DWORD,nullptr,&enabled,&size);
    if(result==ERROR_FILE_NOT_FOUND) return false;
    if(result!=ERROR_SUCCESS || enabled>1) throw std::runtime_error("Adaptive bitrate setting is invalid, Win32="+std::to_string(result));
    return enabled!=0;
}
void publish_active_bitrate(unsigned kbps) {
    const DWORD value=kbps;
    const auto result=RegSetKeyValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"ActiveBitrate",REG_DWORD,&value,sizeof(value));
    if(result!=ERROR_SUCCESS) throw std::runtime_error("Active bitrate write failed, Win32="+std::to_string(result));
    // A zero bitrate means no playback is active. Clear the published format
    // so the panel never shows a stale sample rate beside an idle stream.
    if(kbps==0) publish_active_format(0,0);
}
void publish_active_format(unsigned sample_rate, unsigned bits) {
    const DWORD rate=sample_rate;
    const auto rate_result=RegSetKeyValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"ActiveSampleRate",REG_DWORD,&rate,sizeof(rate));
    if(rate_result!=ERROR_SUCCESS) throw std::runtime_error("Active sample rate write failed, Win32="+std::to_string(rate_result));
    const DWORD depth=bits;
    const auto depth_result=RegSetKeyValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"ActiveBits",REG_DWORD,&depth,sizeof(depth));
    if(depth_result!=ERROR_SUCCESS) throw std::runtime_error("Active bit depth write failed, Win32="+std::to_string(depth_result));
}
void inspect_active_bitrate() {
    // The service publishes the bitrate it is really playing. Reading it back
    // here touches neither the Bluetooth stack nor the device tree, so a panel
    // that polls this while the earbuds play does not query the radio.
    DWORD active=0,active_size=sizeof(active);
    const auto active_error=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"ActiveBitrate",RRF_RT_REG_DWORD,nullptr,&active,&active_size);
    if(active_error!=ERROR_SUCCESS && active_error!=ERROR_FILE_NOT_FOUND) throw std::runtime_error("Active bitrate read failed, Win32="+std::to_string(active_error));
    DWORD active_rate=0,active_rate_size=sizeof(active_rate);
    const auto rate_error=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"ActiveSampleRate",RRF_RT_REG_DWORD,nullptr,&active_rate,&active_rate_size);
    if(rate_error!=ERROR_SUCCESS && rate_error!=ERROR_FILE_NOT_FOUND) throw std::runtime_error("Active sample rate read failed, Win32="+std::to_string(rate_error));
    DWORD active_bits=0,active_bits_size=sizeof(active_bits);
    const auto bits_error=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"ActiveBits",RRF_RT_REG_DWORD,nullptr,&active_bits,&active_bits_size);
    if(bits_error!=ERROR_SUCCESS && bits_error!=ERROR_FILE_NOT_FOUND) throw std::runtime_error("Active bit depth read failed, Win32="+std::to_string(bits_error));
    // A service that is not running has no live stream: the published value
    // would be stale, so it is reported as idle.
    bool running=false;
    const auto manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if (manager) {
        const auto service=OpenServiceW(manager,L"LHDC-Win",SERVICE_QUERY_STATUS);
        if (service) {
            SERVICE_STATUS_PROCESS status{}; DWORD size=0;
            if (QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&size))
                running=status.dwCurrentState==SERVICE_RUNNING;
            CloseServiceHandle(service);
        }
        CloseServiceHandle(manager);
    }
    if (!running) { active=0; active_rate=0; active_bits=0; }
    std::cout << "{\"event\":\"active_bitrate\",\"active_kbps\":"<<active
        << ",\"active_sample_rate\":"<<active_rate<<",\"active_bits\":"<<active_bits
        << ",\"running\":"<<(running?"true":"false")<<"}\n";
}
void inspect_audio_service() {
    const auto manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if (!manager) throw std::runtime_error("SCM status open failed, Win32="+std::to_string(GetLastError()));
    const auto service=OpenServiceW(manager,L"LHDC-Win",SERVICE_QUERY_STATUS);
    const auto error=GetLastError(); CloseServiceHandle(manager);
    if (!service) {
        if (error==ERROR_SERVICE_DOES_NOT_EXIST) { std::cout << "{\"event\":\"audio_service\",\"installed\":false,\"running\":false}\n"; return; }
        throw std::runtime_error("Audio service status open failed, Win32="+std::to_string(error));
    }
    SERVICE_STATUS_PROCESS status{}; DWORD size=0;
    const auto ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&size);
    const auto query_error=GetLastError(); CloseServiceHandle(service);
    if (!ok) throw std::runtime_error("Audio service status query failed, Win32="+std::to_string(query_error));
    HKEY profile=nullptr;
    const auto writable=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",0,KEY_SET_VALUE,&profile);
    if (writable==ERROR_SUCCESS) RegCloseKey(profile);
    else if (writable!=ERROR_ACCESS_DENIED && writable!=ERROR_FILE_NOT_FOUND) throw std::runtime_error("Codec profile access query failed, Win32="+std::to_string(writable));
    avdtp::Bytes caps(4096); DWORD caps_size=static_cast<DWORD>(caps.size());
    const auto cached=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"PeerCapabilities",RRF_RT_REG_BINARY,nullptr,caps.data(),&caps_size);
    if (cached==ERROR_SUCCESS) caps.resize(caps_size);
    else if (cached==ERROR_FILE_NOT_FOUND) caps.clear();
    else throw std::runtime_error("Peer capability cache read failed, Win32="+std::to_string(cached));
    DWORD peer_revision=0,revision_size=sizeof(peer_revision);
    const auto revision_error=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"PeerCapabilityRevision",RRF_RT_REG_DWORD,nullptr,&peer_revision,&revision_size);
    if(revision_error!=ERROR_SUCCESS && revision_error!=ERROR_FILE_NOT_FOUND) throw std::runtime_error("Cached capability revision read failed, Win32="+std::to_string(revision_error));
    const auto current_revision=capability_revision();
    if(peer_revision!=current_revision) caps.clear();
    std::wstring peer_instance(4096,L'\0');DWORD peer_size=static_cast<DWORD>(peer_instance.size()*sizeof(wchar_t));
    const auto peer_error=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"PeerDeviceInstance",RRF_RT_REG_SZ,nullptr,peer_instance.data(),&peer_size);
    if (peer_error!=ERROR_SUCCESS && peer_error!=ERROR_FILE_NOT_FOUND) throw WindowsError("Cached peer identity read",peer_error);
    const auto target=discover_target();
    if (peer_error==ERROR_FILE_NOT_FOUND || !target || !same_instance(peer_instance.c_str(),target->instance)) caps.clear();
    DWORD mtu=0,mtu_size=sizeof(mtu);
    const auto mtu_error=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"PeerMediaMtu",RRF_RT_REG_DWORD,nullptr,&mtu,&mtu_size);
    if(mtu_error!=ERROR_SUCCESS && mtu_error!=ERROR_FILE_NOT_FOUND) throw std::runtime_error("Media MTU cache read failed, Win32="+std::to_string(mtu_error));
    if (caps.empty()) mtu=0;
    DWORD active=0,active_size=sizeof(active);
    const auto active_error=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"ActiveBitrate",RRF_RT_REG_DWORD,nullptr,&active,&active_size);
    if(active_error!=ERROR_SUCCESS && active_error!=ERROR_FILE_NOT_FOUND) throw std::runtime_error("Active bitrate read failed, Win32="+std::to_string(active_error));
    DWORD active_rate=0,active_rate_size=sizeof(active_rate);
    const auto active_rate_error=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"ActiveSampleRate",RRF_RT_REG_DWORD,nullptr,&active_rate,&active_rate_size);
    if(active_rate_error!=ERROR_SUCCESS && active_rate_error!=ERROR_FILE_NOT_FOUND) throw std::runtime_error("Active sample rate read failed, Win32="+std::to_string(active_rate_error));
    DWORD active_bits=0,active_bits_size=sizeof(active_bits);
    const auto active_bits_error=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\LHDC-Win",L"ActiveBits",RRF_RT_REG_DWORD,nullptr,&active_bits,&active_bits_size);
    if(active_bits_error!=ERROR_SUCCESS && active_bits_error!=ERROR_FILE_NOT_FOUND) throw std::runtime_error("Active bit depth read failed, Win32="+std::to_string(active_bits_error));
    if(status.dwCurrentState!=SERVICE_RUNNING || !target || !same_instance(target->service,L"lhdc-transport")) { active=0; active_rate=0; active_bits=0; }
    std::cout << "{\"event\":\"audio_service\",\"installed\":true,\"running\":" << (status.dwCurrentState==SERVICE_RUNNING?"true":"false")
        << ",\"state\":" << status.dwCurrentState << ",\"pid\":" << status.dwProcessId << ",\"profile_editable\":" << (writable==ERROR_SUCCESS?"true":"false")
        << ",\"peer_capabilities\":" << json_string(avdtp::hex(caps)) << ",\"capability_revision\":"<<current_revision<<",\"media_mtu\":"<<mtu
        << ",\"adaptive_bitrate\":"<<(adaptive_bitrate_enabled()?"true":"false")<<",\"active_kbps\":"<<active
        << ",\"active_sample_rate\":"<<active_rate<<",\"active_bits\":"<<active_bits<<"}\n";
}
}
