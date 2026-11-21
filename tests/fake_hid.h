#pragma once
// A scripted HidDev for driver tests: the production driver is linked unchanged, its HID transport
// is replaced by this one. Include in exactly one test .cpp and link the driver with
// src/hid_util.cpp (Hex4, GuessKind, ...). No hardware, no USB writes.
//
//   fake::Device& d = fake::Add(fake::Coll(L"rx", 0x046D, 0xC539, 0xFF00, 0x0002, 20, 20, 0));
//   d.onWrite = [](const std::vector<BYTE>& sent, fake::Device& self) { self.input.push_back(reply); };
//   d.features[0x05] = { 0x05, 0x10, ... };   // GetFeature(0x05)
//
// Reads return queued input reports in order and time out at once when the queue is empty; the
// requested timeouts are summed in fake::waitedMs, so a test can bound a driver's deadline.
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../src/drivers/drivers.h"

namespace fake {

struct Device {
    HidColl coll;
    std::wstring product;
    bool openFails = false;
    std::deque<std::vector<BYTE>> input;                // what Read / SetOutputAndRead return, in order
    std::map<BYTE, std::vector<BYTE>> features;         // GetFeature(id): out[0] = id
    std::map<BYTE, std::vector<BYTE>> inputReports;     // GetInputReport(id)
    std::vector<std::vector<BYTE>> writes;              // Write and SetOutputAndRead requests
    std::vector<std::vector<BYTE>> featuresSet;         // SetFeature requests
    // Called after each Write / SetOutputAndRead / SetFeature; usually queues the device's reply.
    std::function<void(const std::vector<BYTE>& sent, Device& self)> onWrite;
    // Called before GetFeature returns `features[id]`, for replies that change while the driver polls.
    std::function<void(BYTE id, Device& self)> onGetFeature;
    int opens = 0;
};

inline std::map<std::wstring, Device> devices;  // by collection path
inline long long waitedMs = 0;                   // sum of timeouts of reads that found nothing

inline HidColl Coll(const std::wstring& path, USHORT vid, USHORT pid, USHORT page, USHORT usage, USHORT in, USHORT out,
                    USHORT feat, int iface = -1) {
    HidColl c;
    c.path = path;
    c.vid = vid;
    c.pid = pid;
    c.iface = iface;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = in;
    c.caps.OutputReportByteLength = out;
    c.caps.FeatureReportByteLength = feat;
    return c;
}

inline Device& Add(const HidColl& c, const std::wstring& product = L"") {
    Device& d = devices[c.path];
    d.coll = c;
    d.product = product;
    return d;
}

inline void Reset() {
    devices.clear();
    waitedMs = 0;
}

inline Device* Find(const std::wstring& path) {
    auto it = devices.find(path);
    return it == devices.end() ? nullptr : &it->second;
}

}  // namespace fake

bool HidDev::Open(const HidColl& c) {
    c_ = c;
    fake::Device* d = fake::Find(c.path);
    if (!d || d->openFails) return false;
    d->opens++;
    h_ = reinterpret_cast<HANDLE>(1);
    overlapped_ = true;
    return true;
}
bool HidDev::OpenReadOnly(const HidColl& c) { return Open(c); }
bool HidDev::OpenFeatureOnly(const HidColl& c) {
    bool ok = Open(c);
    overlapped_ = false;
    return ok;
}
void HidDev::Close() { h_ = INVALID_HANDLE_VALUE; }

bool HidDev::Write(const std::vector<BYTE>& data, int) {
    fake::Device* d = ok() ? fake::Find(c_.path) : nullptr;
    if (!d || data.empty()) return false;
    std::vector<BYTE> sent(std::max<size_t>(c_.outLen(), data.size()), 0);
    std::copy(data.begin(), data.end(), sent.begin());
    d->writes.push_back(sent);
    if (d->onWrite) d->onWrite(sent, *d);
    return true;
}

int HidDev::Read(std::vector<BYTE>& buf, int timeoutMs) {
    fake::Device* d = ok() ? fake::Find(c_.path) : nullptr;
    if (!d || d->input.empty()) {
        fake::waitedMs += timeoutMs;
        return -1;
    }
    buf = d->input.front();
    d->input.pop_front();
    return (int)buf.size();
}

int HidDev::SetOutputAndRead(const std::vector<BYTE>& data, std::vector<BYTE>& buf, int timeoutMs) {
    if (!Write(data)) return -1;
    return Read(buf, timeoutMs);
}

void HidDev::Flush() {}

bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    fake::Device* d = ok() ? fake::Find(c_.path) : nullptr;
    if (!d || !c_.featLen()) return false;
    d->featuresSet.push_back(data);
    if (d->onWrite) d->onWrite(data, *d);
    return true;
}

bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    fake::Device* d = ok() ? fake::Find(c_.path) : nullptr;
    if (d && d->onGetFeature) d->onGetFeature(reportId, *d);
    if (!d || !d->features.count(reportId)) return false;
    out = d->features[reportId];
    if (c_.featLen() && out.size() < c_.featLen()) out.resize(c_.featLen(), 0);
    return true;
}

bool HidDev::GetFeatureByte(BYTE reportId, BYTE& value, int) {
    std::vector<BYTE> r;
    if (!GetFeature(reportId, r) || r.size() < 2) return false;
    value = r[1];
    return true;
}

bool HidDev::GetInputReport(BYTE reportId, std::vector<BYTE>& out) {
    fake::Device* d = ok() ? fake::Find(c_.path) : nullptr;
    if (!d || !d->inputReports.count(reportId)) return false;
    out = d->inputReports[reportId];
    return true;
}

std::wstring HidProduct(const HidColl& c) {
    fake::Device* d = fake::Find(c.path);
    return d ? d->product : L"";
}
