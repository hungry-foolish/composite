#!/bin/sh

./cos_loader \
"c0.o, ;llboot.o, ;*ds.o, ;mm.o, ;print.o, ;boot.o, ;\
\
!mpool.o,a3;!trans.o,a6;!sm.o,a4;!l.o,a1;!te.o,a3;!e.o,a4;!stat.o,a25;\
!buf.o,a5;!bufp.o, ;!va.o,a2;!(cpu0.o=cpu.o), 'p:6,w:10,b:5':\
\
c0.o-llboot.o;\
ds.o-print.o|[parent_]mm.o|[faulthndlr_]llboot.o;\
mm.o-[parent_]llboot.o|print.o;\
boot.o-print.o|ds.o|mm.o|llboot.o;\
l.o-ds.o|mm.o|print.o;\
te.o-sm.o|print.o|ds.o|mm.o|va.o;\
e.o-sm.o|ds.o|print.o|mm.o|l.o|va.o;\
stat.o-sm.o|te.o|ds.o|l.o|print.o|e.o;\
sm.o-print.o|ds.o|mm.o|boot.o|va.o|l.o|mpool.o;\
buf.o-boot.o|sm.o|ds.o|print.o|l.o|mm.o|va.o|mpool.o;\
bufp.o-sm.o|ds.o|print.o|l.o|mm.o|va.o|mpool.o|buf.o;\
mpool.o-print.o|ds.o|mm.o|boot.o|va.o|l.o;\
va.o-ds.o|print.o|mm.o|l.o|boot.o;\
trans.o-sm.o|ds.o|l.o|buf.o|bufp.o|mm.o|va.o|e.o|print.o;\
cpu0.o-print.o|te.o|sm.o|ds.o\
" ./gen_client_stub
