#!/bin/bash

# Simple HTTP-3/QUIC Client

server_name=$1
server_ip=$(/usr/local/dos-mitigation/common/bin/hostname_to_ip $server_name)
server_port=443
file_size=$2

url="https://$server_ip:$server_port/junk.bin"

curl --http3-only --create-dirs --no-keepalive -H 'Cache-Control: no-cache' $url -o /tmp/http_junk -r 1-$file_size --cacert /usr/local/dos-mitigation/server.pem

