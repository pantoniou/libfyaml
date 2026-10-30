// SPDX-License-Identifier: MIT

/*
 * packed, #pragma pack and bit-fields together, and records without members.
 * The Windows ABIs, MinGW and MSVC, treat these in their own ways.
 */
struct p1 { char a; int b : 12; char c; } __attribute__((packed));
struct p2 { char a; int b : 12 __attribute__((packed)); char c; };
struct p3 { char a; int b; char c; } __attribute__((packed));
struct p4 { char a; short b : 3; int c : 5; } __attribute__((packed));
#pragma pack(push, 1)
struct p5 { char a; int b : 12; char c; };
#pragma pack(pop)
#pragma pack(push, 2)
struct p6 { char a; int b : 12; char c; int d; };
#pragma pack(pop)
struct p7 { char a; int b : 4; } __attribute__((packed, aligned(4)));
union p8 { char a; int b : 12; } __attribute__((packed));

struct e0 { };
union e1 { };
struct e2 { int :0; };
struct e3 { char :0; };
struct e4 { long long :0; };
struct e5 { int a[0]; };
struct e6 { int :3; };
struct e7 { char c; int :0; };
union e8 { char :0; };
union e9 { long long :0; };
union e10 { int :5; };
struct e11 { int :0; } __attribute__((aligned(16)));
struct e12 { struct e0 x; };
struct e13 { char c; struct e0 x; };
