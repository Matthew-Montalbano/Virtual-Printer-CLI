# Virtual-Printer-CLI
A command line tool written in C allowing users to print and convert files using virtual printers.<br>
Concurrent printing is achieved through the use of process forking.

## Features
- Users can virtually print up to 32 files concurrently
- Multiple conversions between any file types are possible, given that conversion programs are supplied to the CLI
- Queue system allows up to 64 print jobs to be put into the system at a time, with print jobs starting automatically once a valid printer becomes available available
