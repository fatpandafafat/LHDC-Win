#pragma once
#include <filesystem>
namespace lhdc {
// A driver store package keeps the binary it was published with. When the
// package is republished with an unchanged INF, Windows reuses the published
// folder and the previous driver image stays in place, so a rebuilt driver is
// silently not installed. This compares the driver image the system loaded
// with the payload that was staged and refuses to continue when they differ.
void verify_driver_payload(const std::filesystem::path& staged_sys,const wchar_t* service);
}
