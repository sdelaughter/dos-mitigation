#!/bin/bash

sudo sysctl -w net.ipv4.tcp_syncookies=$1
sudo sysctl -p
