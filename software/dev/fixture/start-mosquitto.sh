#!/bin/sh
set -eu
umask 077
cp /local/broker-passwords /tmp/passwords
mosquitto_passwd -U /tmp/passwords
cp /local/server.key /tmp/server.key
cp /local/server.pem /tmp/server.pem
cp /local/ca.pem /tmp/ca.pem
cp /local/broker.acl /tmp/broker.acl
chown mosquitto:mosquitto /tmp/passwords /tmp/server.key /tmp/server.pem /tmp/ca.pem /tmp/broker.acl
exec mosquitto -c /fixture/mosquitto.conf
