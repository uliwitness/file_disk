//
//  main.cpp
//  FileDisk
//
//  Created by Uli Kusterer on 09/08/15.
//  Copyright (c) 2015 Uli Kusterer. All rights reserved.
//

#include <iostream>
#include "file_disk.h"


using namespace std;


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
    
//    if( !theFile.delete_file( "hello_world.txt" ) )
//        cout << "File of name 'hello_world.txt' doesn't exist on this file_disk." << endl;
    
    if( !theFile.compact() )
        cout << "Could not compact the file_disk." << endl;
    
//    if( !theFile.write() )
//        cout << "Could not save this file_disk." << endl;
    return 0;
}
