#!/bin/bash

ssn_acc=1;
obj_dst=1;
obj_src=3;
url="http://localhost:10001";
message_type=2;

msg_data='{"ssn":{"v":1,"obj":1,"cmd":"getdevvals", "data": {"g":1}}}';

len=${#msg_data};
crc=$(./crc16 "$msg_data" $len);

printf -v msg "===ssn1%04x%04x%02x%04x%s%04x" "$obj_dst" "$obj_src" "$message_type" "$len" "$msg_data" "$crc";

curl --raw -X POST $url --data "$msg"
