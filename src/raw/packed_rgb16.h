#pragma once
#include <QtGlobal>
// Destination row capacity must be at least 4*width native-endian 16-bit values.
// Input is packed R,G,B in the first 3*width values of the same row.
inline void expandPackedRgb16(quint16 *row,int width) {
    for(int x=width-1;x>=0;--x) {
        const auto r=row[x*3],g=row[x*3+1],b=row[x*3+2];
        row[x*4]=r; row[x*4+1]=g; row[x*4+2]=b; row[x*4+3]=65535;
    }
}
