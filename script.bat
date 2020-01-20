scp %1.c user@193.170.192.225:/home/user/Treiber
scp Makefile user@193.170.192.225:/home/user/Treiber
ssh user@193.170.192.225
scp user@193.170.192.225:/home/user/Treiber/%1.ko %1.ko
scp %1.ko root@%2:/home/root/Treiber/
ssh root@%2
