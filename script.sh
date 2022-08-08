rm out/*
gcc -g opePanel1.c -o out/p1 -lpthread -lrt
gcc -g liftMng.c -o out/liftMng -lpthread -lrt
gcc -g liftCtrl.c -o out/liftCtrl -lpthread -lrt
