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
#include "index_set.h"
#include <sstream>


using namespace std;
using namespace fld;


void    print_statistics( const struct stats& statistics )
{
    cout << "No. of files in file_disk: " << internal << setw(5) << statistics.num_files << endl;
    cout << "Used data:                 " << internal << setw(5) << statistics.used_bytes << " bytes" << endl;
    cout << "Wasted data:               " << internal << setw(5) << statistics.free_bytes << " bytes" << endl;
    cout << "Map size:                  " << internal << setw(5) << statistics.map_bytes << " bytes" << endl;
    cout << "    of that names:         " << internal << setw(5) << statistics.name_bytes << " bytes" << endl;
    cout << "Header:                    " << internal << setw(5) << statistics.header_bytes << " bytes" << endl;
    cout << "=============================================" << endl;
    cout << "Total file size:           " << internal << setw(5) << (statistics.used_bytes + statistics.free_bytes + statistics.map_bytes +statistics.header_bytes) << " bytes" << endl << endl;
}


void    test_indexes()
{
    stringstream            dumped;
    index_set<uint64_t>     indexes;
    indexes.append( 1 );
    if( indexes.has(0) != index_set<uint64_t>::does_not_exist )
        cout << "error: Adding 1 added 0!" << std::endl;
    if( indexes.has(1) != index_set<uint64_t>::fully_exists )
        cout << "error: Adding 1 failed!" << std::endl;
    if( indexes.has(1,2) != index_set<uint64_t>::partially_exists )
        cout << "error: Adding 1 added 2 or didn't add 1!" << std::endl;
    if( indexes.has(2) != index_set<uint64_t>::does_not_exist )
        cout << "error: Adding 1 added 2!" << std::endl;
    indexes.append( 2 );
    if( indexes.has(1) != index_set<uint64_t>::fully_exists )
        cout << "error: Adding 2 destroyed 1!" << std::endl;
    if( indexes.has(2) != index_set<uint64_t>::fully_exists )
        cout << "error: Adding 2 failed!" << std::endl;
    if( indexes.has(1,2) != index_set<uint64_t>::fully_exists )
        cout << "error: Adding 1 and 2 somehow went wrong!" << std::endl;
    indexes.append( 3 );
    if( indexes.has(1) != index_set<uint64_t>::fully_exists )
        cout << "error: Adding 3 destroyed 1!" << std::endl;
    if( indexes.has(2) != index_set<uint64_t>::fully_exists )
        cout << "error: Adding 3 destroyed 2!" << std::endl;
    if( indexes.has(3) != index_set<uint64_t>::fully_exists )
        cout << "error: Adding 2 failed!" << std::endl;
    if( indexes.has(1,3) != index_set<uint64_t>::fully_exists )
        cout << "error: Adding 1 through 3 somehow went wrong!" << std::endl;
    indexes.print( dumped );
    if( dumped.str().compare( "1 ranges:\n{ 1, 3 }\n" ) != 0 )
        cout << "error: Sequential adding test failed!" << std::endl;

    stringstream            dumped2;
    index_set<uint64_t>     indexes2;
    indexes2.append( 1 );
    indexes2.append( 3 );
    indexes2.append( 2 );
    indexes2.print( dumped2 );
    if( dumped2.str().compare( "1 ranges:\n{ 1, 3 }\n" ) != 0 )
        cout << "error: Out-of-order adding test failed!" << endl << dumped2.str() << endl;

    stringstream            dumped3;
    index_set<uint64_t>     indexes3;
    indexes3.append( 1 );
    indexes3.append( 3 );
    indexes3.append( 5 );
    indexes3.append( 7 );
    indexes3.print( dumped3 );
    if( dumped3.str().compare( "4 ranges:\n{ 1, 1 }\n{ 3, 3 }\n{ 5, 5 }\n{ 7, 7 }\n" ) != 0 )
        cout << "error: Gappy adding test failed!" << endl << dumped3.str() << endl;
}


int main(int argc, const char * argv[])
{
    test_indexes();
    
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
    
    theFile.print( cout );
    struct stats   statistics;
    theFile.statistics( &statistics );
    print_statistics( statistics );
    
    if( !theFile.is_valid() )
    {
        cout << "*** ERROR FILE IS NO LONGER VALID ***" << endl;
        theFile.print( cout );
    }

    const char* theStr2 = "Compacted.";
    size_t      dataLen2 = strlen(theStr2);
    char*       blockData2 = new char[dataLen2];
    memmove( blockData2, theStr2, dataLen2 );
    
    if( !theFile.set_file_contents( "hello_world.txt", blockData2, dataLen2) )
        cout << "Couldn't change block text." << endl;

    if( !theFile.write() )
        cout << "Could not save this file_disk." << endl;

    theFile.print( cout );
    theFile.statistics( &statistics );
    print_statistics( statistics );
    
    if( !theFile.is_valid() )
    {
        cout << "*** ERROR FILE IS NO LONGER VALID ***" << endl;
        theFile.print( cout );
    }
    
    if( !theFile.delete_file( "hello_world.txt" ) )
        cout << "File of name 'hello_world.txt' doesn't exist on this file_disk." << endl;
    
    if( !theFile.compact() )
        cout << "Could not compact the file_disk." << endl;
    
    theFile.print( cout );
    theFile.statistics( &statistics );
    print_statistics( statistics );
    
    if( !theFile.is_valid() )
    {
        cout << "*** ERROR FILE IS NO LONGER VALID ***" << endl;
        theFile.print( cout );
    }
    
    return 0;
}
