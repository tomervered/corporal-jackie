# broker — Stream broker

Turns a torrent file into seekable bytes: piece deadlines, read-ahead and a bounded cache. The only code allowed to call libtorrent, and it never knows who is reading. Arrives in Step 3.
