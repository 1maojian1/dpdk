cmd_dpdk_udppkt.o = gcc -Wp,-MD,./.dpdk_udppkt.o.d.tmp  -m64 -pthread -I/home/king/share/dpdk/dpdk-stable-19.08.2//lib/librte_eal/linux/eal/include  -march=native -DRTE_MACHINE_CPUFLAG_SSE -DRTE_MACHINE_CPUFLAG_SSE2 -DRTE_MACHINE_CPUFLAG_SSE3 -DRTE_MACHINE_CPUFLAG_SSSE3 -DRTE_MACHINE_CPUFLAG_SSE4_1 -DRTE_MACHINE_CPUFLAG_SSE4_2 -DRTE_MACHINE_CPUFLAG_AES -DRTE_MACHINE_CPUFLAG_PCLMULQDQ -DRTE_MACHINE_CPUFLAG_AVX -DRTE_MACHINE_CPUFLAG_RDRAND -DRTE_MACHINE_CPUFLAG_FSGSBASE -DRTE_MACHINE_CPUFLAG_F16C -DRTE_MACHINE_CPUFLAG_AVX2  -I/home/king/share/dpdk/dpdk-stable-19.08.2/examples/dpdk-vip/build/include -I/home/king/share/dpdk/dpdk-stable-19.08.2//x86_64-native-linux-gcc/include -include /home/king/share/dpdk/dpdk-stable-19.08.2//x86_64-native-linux-gcc/include/rte_config.h -D_GNU_SOURCE -O3 -W -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wold-style-definition -Wpointer-arith -Wcast-align -Wnested-externs -Wcast-qual -Wformat-nonliteral -Wformat-security -Wundef -Wwrite-strings -Wdeprecated -Wno-address-of-packed-member    -o dpdk_udppkt.o -c /home/king/share/dpdk/dpdk-stable-19.08.2/examples/dpdk-vip/dpdk_udppkt.c 
