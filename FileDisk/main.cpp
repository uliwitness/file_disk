//
//  main.cpp
//  FileDisk
//
//  Created by Uli Kusterer on 09/08/15.
//  Copyright (c) 2015 Uli Kusterer. All rights reserved.
//

#include <iostream>
#include "file_disk.h"
#include <iomanip>


using namespace std;
using namespace fld;


void    print_statistics( const struct stats& statistics )
{
    cout << "No. of files in file_disk: " << internal << setw(5) << statistics.num_files << endl;
    cout << "Used data:                 " << internal << setw(5) << statistics.used_bytes << " bytes" << endl;
    cout << "Wasted data:               " << internal << setw(5) << statistics.free_bytes << " bytes" << endl;
    cout << "Map size:                  " << internal << setw(5) << statistics.map_bytes << " bytes" << endl;
    cout << "Header:                    " << internal << setw(5) << statistics.header_bytes << " bytes" << endl;
    cout << "Names in map:              " << internal << setw(5) << statistics.name_bytes << " bytes" << endl;
    cout << "=============================================" << endl;
    cout << "Total file size:           " << internal << setw(5) << (statistics.used_bytes + statistics.free_bytes + statistics.map_bytes +statistics.header_bytes) << " bytes" << endl << endl;
}


int main(int argc, const char * argv[])
{
    file_disk   theFile;
    if( !theFile.open("testfile.boff") )
        cout << "Couldn't create a new disk_file of name 'hello_world.txt'." << endl;
    
    const char* theStr = "Hello, World!";
    size_t      dataLen = strlen(theStr);
    char*       blockData = new char[dataLen];
    memmove( blockData, theStr, dataLen );
    
    if( !theFile.add_file( "hello_world.txt", blockData, dataLen ) )
        cout << "File of name 'hello_world.txt' already exists on this file_disk." << endl;
    
    if( !theFile.write() )
        cout << "Could not save this file_disk." << endl;
    
    struct stats   statistics;
    theFile.statistics( &statistics );
    print_statistics( statistics );

    const char* theStr2 = "Compacted.";
    size_t      dataLen2 = strlen(theStr2);
    char*       blockData2 = new char[dataLen2];
    memmove( blockData2, theStr2, dataLen2 );
    
    if( !theFile.set_file_contents( "hello_world.txt", blockData2, dataLen2) )
        cout << "Could change block text." << endl;

    if( !theFile.write() )
        cout << "Could not save this file_disk." << endl;

    theFile.statistics( &statistics );
    print_statistics( statistics );
    
    if( !theFile.delete_file( "hello_world.txt" ) )
        cout << "File of name 'hello_world.txt' doesn't exist on this file_disk." << endl;
    
    if( !theFile.compact() )
        cout << "Could not compact the file_disk." << endl;
    
    theFile.statistics( &statistics );
    print_statistics( statistics );
    
    return 0;
}
