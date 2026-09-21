// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_geometry.h"
#include "gx_vertex_format.h"
namespace mkw::agc {
// Serializes metadata, raw FIFO vertices and owned indexed-array intervals.
// The renderer uploads this immutable vector as one byte-addressed GPU buffer
// and keeps that allocation alive until its draws retire. No host pointers.
std::vector<uint8_t> serialize_gx_geometry(const GxGeometry&);
}
