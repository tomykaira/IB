# ibv_devinfo

    hca_id: mlx4_0
        transport:                      InfiniBand (0)
        fw_ver:                         2.8.000
        node_guid:                      0002:c903:000e:8f76
        sys_image_guid:                 0002:c903:000e:8f79
        vendor_id:                      0x02c9
        vendor_part_id:                 26428
        hw_ver:                         0xB0
        board_id:                       HP_0180000009
        phys_port_cnt:                  2
                port:   1
                        state:                  PORT_ACTIVE (4)
                        max_mtu:                2048 (4)
                        active_mtu:             2048 (4)
                        sm_lid:                 1
                        port_lid:               17
                        port_lmc:               0x00
                        link_layer:             IB

                port:   2
                        state:                  PORT_DOWN (1)
                        max_mtu:                2048 (4)
                        active_mtu:             2048 (4)
                        sm_lid:                 0
                        port_lid:               0
                        port_lmc:               0x00
                        link_layer:             IB

# start up log

    [0] nprocs(2)
    [0] found 1 IB device(s)
    [0] IB device name: mlx4_0
    [0] IB context = 23998f0
    [0] fixed MR was registered with addr=0x239bd80, lkey=0xd0001f36, rkey=0xd0001f36, flags=0x7
    [0] QP was created, QP number=0x64004d
    [1] nprocs(2)
    [1] found 1 IB device(s)
    [1] IB device name: mlx4_0
    [1] IB context = 5d9f8c0
    [1] fixed MR was registered with addr=0x5da5300, lkey=0x38001f2f, rkey=0x38001f2f, flags=0x7
    [1] QP was created, QP number=0x78004d

# references

[ワークキュー - InfiniBand.JP/Wiki](http://www.infiniband.jp/wiki/wiki.cgi?page=%A5%EF%A1%BC%A5%AF%A5%AD%A5%E5%A1%BC)
[InfiniBand解説 - メラノックス テクノロジーズ ジャパン株式会社：Mellanox Technologies Japan - InfiniBand](http://www.mellanox.co.jp/infiniband/)

[InfiniBand, Verbs, RDMA | The Geek in the Corner](http://thegeekinthecorner.wordpress.com/category/infiniband-verbs-rdma/)
