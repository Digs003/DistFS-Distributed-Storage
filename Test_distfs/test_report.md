# DistFS Test Report
Generated on: Wed Apr 15 15:53:46 IST 2026

## 1. Initialization
### Build Log
```
#17 exporting manifest list sha256:9cd0ccbecfd2e03d20069826240bb751be055277405f297c78f9e6f288e0605f 0.0s done
#17 naming to docker.io/library/test_distfs-node1:latest done
#17 unpacking to docker.io/library/test_distfs-node1:latest 0.1s done
#17 DONE 0.4s

#18 [node0] resolving provenance for metadata file
#18 DONE 0.1s

#19 [node1] resolving provenance for metadata file
#19 DONE 0.0s

#20 [node2] resolving provenance for metadata file
#20 DONE 0.1s

#21 [client] resolving provenance for metadata file
#21 DONE 0.1s
 Image test_distfs-node1 Built 
 Image test_distfs-node2 Built 
 Image test_distfs-client Built 
 Image test_distfs-node0 Built 
```

## 2. Cluster Startup & Leader Election
```

=== DistFS Cluster Status ===
Metadata Cluster:
  Leader:    node-2
  Term:      1 | Log Index: 0
Storage Daemons:
  node0:6001  [ALIVE]  0 GB / 452.133 GB
  node2:6003  [ALIVE]  0 GB / 452.133 GB
  node1:6002  [ALIVE]  0 GB / 452.133 GB
Replication Health:
  Total chunks: 0   Under-replicated: 0   Orphaned: 0
```

## 3. Test File Generation
| File Name | Size | Format |
|-----------|------|--------|
| file_1.txt | 5MB | txt |
| file_2.bin | 5MB | bin |
| file_3.log | 3MB | log |
| file_4.dat | 5MB | dat |
| file_5.csv | 4MB | csv |
| file_6.md | 1MB | md |
| file_7.json | 3MB | json |
| file_8.xml | 1MB | xml |
| file_9.conf | 1MB | conf |
| file_10.tmp | 4MB | tmp |

## 4. File Upload & Chunk Replication
### Uploading file_1.txt
Local path: `test_files/file_1.txt`
```
[1/4] Chunking: /tmp/file_1.txt
      Chunk 0: sha256=1f853c46... (4 MB)
      Chunk 1: sha256=39b1e9f1... (1 MB)
[2/4] Contacting Metadata Server...
      Received placement plan (2 chunks)
[3/4] Uploading chunks...
      Chunk 0 → Primary: daemon-A | Secondary: daemon-C [OK]
      Chunk 1 → Primary: daemon-A | Secondary: daemon-C [OK]
[4/4] Committing metadata...
      File registered. Revision: 1000
Upload complete. 2 chunks, 2 replicas each.
```

### Uploading file_2.bin
Local path: `test_files/file_2.bin`
```
[1/4] Chunking: /tmp/file_2.bin
      Chunk 0: sha256=981e2c12... (4 MB)
      Chunk 1: sha256=8ec0d06d... (1 MB)
[2/4] Contacting Metadata Server...
      Received placement plan (2 chunks)
[3/4] Uploading chunks...
      Chunk 0 → Primary: daemon-A | Secondary: daemon-C [OK]
      Chunk 1 → Primary: daemon-A | Secondary: daemon-C [OK]
[4/4] Committing metadata...
      File registered. Revision: 1001
Upload complete. 2 chunks, 2 replicas each.
```

### Uploading file_3.log
Local path: `test_files/file_3.log`
```
[1/4] Chunking: /tmp/file_3.log
      Chunk 0: sha256=ea6b697e... (3 MB)
[2/4] Contacting Metadata Server...
      Received placement plan (1 chunks)
[3/4] Uploading chunks...
      Chunk 0 → Primary: daemon-A | Secondary: daemon-C [OK]
[4/4] Committing metadata...
      File registered. Revision: 1002
Upload complete. 1 chunks, 2 replicas each.
```

### Uploading file_4.dat
Local path: `test_files/file_4.dat`
```
[1/4] Chunking: /tmp/file_4.dat
      Chunk 0: sha256=da515c5d... (4 MB)
      Chunk 1: sha256=fa3e5b84... (1 MB)
[2/4] Contacting Metadata Server...
      Received placement plan (2 chunks)
[3/4] Uploading chunks...
      Chunk 0 → Primary: daemon-A | Secondary: daemon-C [OK]
      Chunk 1 → Primary: daemon-A | Secondary: daemon-C [OK]
[4/4] Committing metadata...
      File registered. Revision: 1003
Upload complete. 2 chunks, 2 replicas each.
```

### Uploading file_5.csv
Local path: `test_files/file_5.csv`
```
[1/4] Chunking: /tmp/file_5.csv
      Chunk 0: sha256=61700860... (4 MB)
[2/4] Contacting Metadata Server...
      Received placement plan (1 chunks)
[3/4] Uploading chunks...
      Chunk 0 → Primary: daemon-A | Secondary: daemon-C [OK]
[4/4] Committing metadata...
      File registered. Revision: 1004
Upload complete. 1 chunks, 2 replicas each.
```

### Uploading file_6.md
Local path: `test_files/file_6.md`
```
[1/4] Chunking: /tmp/file_6.md
      Chunk 0: sha256=e615f002... (1 MB)
[2/4] Contacting Metadata Server...
      Received placement plan (1 chunks)
[3/4] Uploading chunks...
      Chunk 0 → Primary: daemon-A | Secondary: daemon-C [OK]
[4/4] Committing metadata...
      File registered. Revision: 1005
Upload complete. 1 chunks, 2 replicas each.
```

### Uploading file_7.json
Local path: `test_files/file_7.json`
```
[1/4] Chunking: /tmp/file_7.json
      Chunk 0: sha256=166f4e40... (3 MB)
[2/4] Contacting Metadata Server...
      Received placement plan (1 chunks)
[3/4] Uploading chunks...
      Chunk 0 → Primary: daemon-C | Secondary: daemon-B [OK]
[4/4] Committing metadata...
      File registered. Revision: 1006
Upload complete. 1 chunks, 2 replicas each.
```

### Uploading file_8.xml
Local path: `test_files/file_8.xml`
```
[1/4] Chunking: /tmp/file_8.xml
      Chunk 0: sha256=127f9877... (1 MB)
[2/4] Contacting Metadata Server...
      Received placement plan (1 chunks)
[3/4] Uploading chunks...
      Chunk 0 → Primary: daemon-C | Secondary: daemon-B [OK]
[4/4] Committing metadata...
      File registered. Revision: 1007
Upload complete. 1 chunks, 2 replicas each.
```

### Uploading file_9.conf
Local path: `test_files/file_9.conf`
```
[1/4] Chunking: /tmp/file_9.conf
      Chunk 0: sha256=508773c9... (1 MB)
[2/4] Contacting Metadata Server...
      Received placement plan (1 chunks)
[3/4] Uploading chunks...
      Chunk 0 → Primary: daemon-C | Secondary: daemon-B [OK]
[4/4] Committing metadata...
      File registered. Revision: 1008
Upload complete. 1 chunks, 2 replicas each.
```

### Uploading file_10.tmp
Local path: `test_files/file_10.tmp`
```
[1/4] Chunking: /tmp/file_10.tmp
      Chunk 0: sha256=38eec24e... (4 MB)
[2/4] Contacting Metadata Server...
      Received placement plan (1 chunks)
[3/4] Uploading chunks...
      Chunk 0 → Primary: daemon-C | Secondary: daemon-B [OK]
[4/4] Committing metadata...
      File registered. Revision: 1009
Upload complete. 1 chunks, 2 replicas each.
```

## 5. Metadata Verification (List)
```
NAME                          CHUNKS    SIZE          REVISION
------------------------------------------------------------
file_10.tmp                   1         4 MB          1009
file_9.conf                   1         1 MB          1008
file_8.xml                    1         1 MB          1007
file_7.json                   1         3 MB          1006
file_4.dat                    2         5 MB          1003
file_3.log                    1         3 MB          1002
file_5.csv                    1         4 MB          1004
file_2.bin                    2         5 MB          1001
file_6.md                     1         1 MB          1005
file_1.txt                    2         5 MB          1000
```

## 6. Deletion Test: file_5.csv
Verifying it's gone from the file list:
```
NAME                          CHUNKS    SIZE          REVISION
------------------------------------------------------------
file_10.tmp                   1         4 MB          1009
file_9.conf                   1         1 MB          1008
file_8.xml                    1         1 MB          1007
file_7.json                   1         3 MB          1006
file_4.dat                    2         5 MB          1003
file_3.log                    1         3 MB          1002
file_2.bin                    2         5 MB          1001
file_6.md                     1         1 MB          1005
file_1.txt                    2         5 MB          1000
```

Checking storage nodes for residual chunks...
Note: The system marks chunks as orphaned. Physical deletion is handled by a pending GC process.
### Node: node0 Storage Listing
```
/var/distfs/chunks:
1f
39
61
8e
98
da
e6
ea
fa

/var/distfs/chunks/1f:
1f853c4674071c4ca155685b2670faf0d3243454e16ecec0c582c862fcc19d69.bin

/var/distfs/chunks/39:
39b1e9f16e40deb78c926b6e0da7ad48fc301f046c5d379e2bf99e0acbca8392.bin

/var/distfs/chunks/61:
61700860992692edd3f2797ecb4578b4773b9da5cfadbcdb2cae386702514d96.bin

/var/distfs/chunks/8e:
8ec0d06ddd9eecb5762ec745565ad309909b3ff8d2bf613a3ba24e9c4ad3d9ce.bin

/var/distfs/chunks/98:
981e2c122dff17673083ae21702ad7f4894f03eae79197f30fca1c50dbc4f6c9.bin

/var/distfs/chunks/da:
da515c5d4ba887205e648b2a98d83c73527e8f39741bafe5d6299310604a7f6f.bin

/var/distfs/chunks/e6:
e615f00228ba10d0ef295de3eecb1e144ba304a2cce687e97755a109096221dc.bin

/var/distfs/chunks/ea:
ea6b697e05e01b333e65ea86a684fa4b7d910f3576c70ce90461bd0cbc1a43a2.bin

/var/distfs/chunks/fa:
fa3e5b8491144cb933a2e0c898e4ee6a6bf73e2781a422373a7340afbd669e98.bin
```

### Node: node1 Storage Listing
```
/var/distfs/chunks:
```

### Node: node2 Storage Listing
```
/var/distfs/chunks:
12
16
38
50

/var/distfs/chunks/12:
127f987757c263909b3c4de3016c3d46879fdd76285645439b829318765f5b86.bin

/var/distfs/chunks/16:
166f4e408ace46bebb9d94d384344f1079b6fa729f507ef7a7a9c0666a19e57d.bin

/var/distfs/chunks/38:
38eec24ea9324aa22d31f05cd04b7f2e9d5fc36be400830205a13045770f6969.bin

/var/distfs/chunks/50:
508773c9c8b0f743db2b2c5f2083bd64fc834e9d1dff42a9fbcb37cba1a5e1dd.bin
```

## 7. Final Cluster Status
```

=== DistFS Cluster Status ===
Metadata Cluster:
  Leader:    node-2
  Term:      1 | Log Index: 11
Storage Daemons:
  node0:6001  [ALIVE]  0.0224609 GB / 452.133 GB
  node2:6003  [ALIVE]  0 GB / 452.133 GB
  node1:6002  [ALIVE]  0 GB / 452.133 GB
Replication Health:
  Total chunks: 13   Under-replicated: 0   Orphaned: 1
```
