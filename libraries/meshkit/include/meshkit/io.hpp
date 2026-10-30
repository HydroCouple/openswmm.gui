// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mesh.hpp"
#include <filesystem>
#include <functional>
#include <istream>

namespace meshkit {
enum class UnknownRecords { Reject,IgnoreWithReport };
struct IOOptions {
    Limits limits;
    std::function<bool()> cancelled;
    bool overwrite=false,durable=false,allowAttributeLoss=false;
    bool legacy2dm=false; ///< Compatibility ids/optional materials; still rejects malformed geometry.
    UnknownRecords unknownRecords=UnknownRecords::Reject;
};
struct IOReport:OperationReport {
    std::uint64_t bytes=0,ignoredRecords=0,reorientedCells=0;
    std::vector<std::string> losses; ///< Bounded codes, with total ignored-record count above.
};
struct ReadResult {Mesh mesh;IOReport report;};
// Version-1 MKB is lossless for the generic model, with counted little-endian
// records and a trailing content checksum. No host struct/padding is serialized.
// Read failures expose no partial mesh. Writes use an exclusive same-directory
// temporary and atomic publication; cancellation/failure preserves an old file.
ReadResult readBinary(std::istream &,const IOOptions & = {});
ReadResult readBinary(const std::filesystem::path &,const IOOptions & = {});
IOReport writeBinary(const std::filesystem::path &,const Mesh &,const IOOptions & = {});
// Bounded-record, two-pass seekable file reader: elements may precede nodes.
// E3T/E4Q/ND/standard NS supported. Unsupported cards are never silently lost.
// 2DM does not declare physical units; callers must supply them before meshing.
ReadResult read2dm(const std::filesystem::path &,const IOOptions & = {});
IOReport write2dm(const std::filesystem::path &,const Mesh &,const IOOptions & = {});
}
