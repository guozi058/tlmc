====
TLMC
====

The Last Mile Cache. For more information, please visit

http://tlmc.fredan.se


====================
Apache Trafficserver
====================

Under the directory trafficserver you will find the hash based
plugin. Please see the comment in the beginning of the file hash_remap.c 
for examples of how to use this.

In TLMC this plugin and TS itself is supposed to be used at an ISP:s customer
to be able to cache content at their home.

The hash based host is to tell the ISP what content they (their customer) is 
trying to download. The ISP can use this to direct to different servers where they 
have the content hosted/cached. Popular content could be hosted on servers closer
to the customer and not so popular further away in their network core.
