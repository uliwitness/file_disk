//
//  file_disk.cpp
//  FileDisk
//
//  Created by Uli Kusterer on 09/08/15.
//  Copyright (c) 2015 Uli Kusterer. All rights reserved.
//

#include "file_disk.h"
#include "index_set.h"
#include <iostream>
#include <sys/stat.h>
#include <sstream>


using namespace std;


namespace fld
{

const char* MAP_BLOCK_FILENAME = "";  // File name we give the map block in the map. This is an invalid file name, so should be fine.


//class block_streambuf : public streambuf
//{
//public:
//    block_streambuf( file_disk& inFileDisk, file_node& inFileNode )
//      : mFileDisk(inFileDisk), mFileNode(inFileNode)
//    {
//    }
//
//protected:
//    virtual int overflow(int c)
//    {
//        char    ch = c;
//        mFileDisk.write_to_node( (char*)&ch, sizeof(ch), mFileNode );
//        return c;
//    }
//
//    virtual int underflow()
//    {
//        int result = EOF;
//        if( mFileDisk.read( &mByte, sizeof(mByte), mFileNode ) )
//            result = mByte;
//        setg( &mByte, &mByte, &mByte+1 );
//        return mByte;
//    }
//
//protected:
//    file_node&  mFileNode;
//    file_disk&  mFileDisk;
//    char        mByte;
//};


file_disk::file_disk()
    : mVersion(0x00000101), mMapOffset(0), mMapFlags(0)
{
    
}


file_disk::~file_disk()
{
    
}


bool    file_disk::open( const std::string& inPath )
{
    mFilePath = inPath;
    mFile.open( mFilePath.c_str(), ios::binary | ios::in | ios::out );
    if( !mFile.is_open() )  // File doesn't exist?
        mFile.open( mFilePath.c_str(), ios::binary | ios::in | ios::out | ios::trunc );    // Create it!
    if( !mFile.is_open() )
        return false;
    mFile.seekg( 0, ios::end );
    mFileSize = mFile.tellg();
    if( mFileSize == 0 )
        mMapFlags = map_needs_rewrite | offsets_dirty | data_dirty;
    
    return load_map();
}


bool    file_disk::load_map()
{
    if( mFileSize > 0 )
    {
        mFile.seekg( 0, ios::beg );
        mFile.read( (char*)&mVersion, sizeof(mVersion) );
        if( (mVersion & 0x0000ff00) != 0x0100 )   // Major version not 1? Incompatible change.
            return false;
        if( (mVersion & 0x000000ff) != 0x01 )   // Minor version not 1? Compatible change.
        {
            cout << "New file format variant " << (mVersion & 0x000000ff) << " some data may be lost if you edit the file." << endl;
        }
        mFile.read( (char*)&mMapOffset, sizeof(mMapOffset) );
        mFile.seekg( mMapOffset, ios::beg );
        uint64_t    numFiles = 0;
        mFile.read( (char*)&numFiles, sizeof(numFiles) );
        for( uint64_t x = 0; x < numFiles; x++ )
        {
            file_node   newNode;
            if( newNode.read(mFile) )
            {
                if( newNode.flags() & file_node::is_free )
                    mFreeBlocks.push_back( newNode );
                else
                    mFileMap[newNode.name()] = newNode;
            }
        }
    }
    
    return true;
}


void    file_disk::swap_node_for_free_node_of_size( file_node& ioNode, size_t desiredSize, size_t desiredSizeIfNotRecycled )
{
    if( desiredSizeIfNotRecycled <= 0 )
        desiredSizeIfNotRecycled = desiredSize;

    for( file_node& currNode : mFreeBlocks )
    {
        if( currNode.physical_size() >= desiredSize )
        {
            file_node   tmp = ioNode;
            ioNode = currNode;
            currNode = tmp;
            ioNode.set_name( currNode.name() );
            file_node::node_flags_t    nodeFlags = file_node::is_free;
            ioNode.set_flags( (currNode.flags() | file_node::offsets_dirty) & ~nodeFlags );
            ioNode.set_logical_size( desiredSize );
            ioNode.set_cached_data( currNode.cached_data() );
            currNode.set_flags(file_node::is_free);
            currNode.set_cached_data( nullptr );
            mMapFlags |= offsets_dirty;
            return;
        }
    }
    
    // If we get here, there's no free node large enough to hold our data, we need to allocate a new one:
    // So mark the old node as free space:
    file_node   oldNode = ioNode;
    oldNode.set_name("");   // Delete the name, would only take up unneeded space or leak confidential data.
    oldNode.set_flags( file_node::is_free );
    mFreeBlocks.push_back( oldNode );
    mMapFlags |= map_needs_rewrite; // Make sure we write out the new free block entry.
    
    // Now allocate space at the end of the file for the new node:
    ioNode.set_start_offset( mFileSize );
    ioNode.set_physical_size( desiredSizeIfNotRecycled );
    ioNode.set_logical_size( desiredSizeIfNotRecycled );
    ioNode.set_flags( ioNode.flags() | file_node::offsets_dirty );
    mFileSize += desiredSizeIfNotRecycled;
}


file_node&    file_disk::node_of_size_for_name( size_t desiredSize, const std::string& inName, size_t desiredSizeIfNotRecycled )
{
    if( desiredSizeIfNotRecycled <= 0 )
        desiredSizeIfNotRecycled = desiredSize;
    
    for( auto itty = mFreeBlocks.begin(); itty != mFreeBlocks.end(); itty++ )
    {
        file_node& currNode = *itty;
        if( currNode.physical_size() >= desiredSize )
        {
            file_node   tmp = currNode;
            tmp.set_name( inName );
            
            mFreeBlocks.erase( itty );
            mFileMap[inName] = tmp;
            tmp.set_flags( file_node::name_dirty | file_node::offsets_dirty );
            // No need to change mMapFlags, we removed a dirty entry and added a new one.
            
            return mFileMap[inName];
        }
    }

    // If we get here, there's no free node large enough to hold our data, we need to allocate a new one:
    file_node   tmp;
    tmp.set_name( inName );
    tmp.set_start_offset( mFileSize );
    tmp.set_physical_size( desiredSizeIfNotRecycled );
    tmp.set_logical_size( desiredSizeIfNotRecycled );
    tmp.set_flags( file_node::name_dirty | file_node::offsets_dirty );
    mFileSize += desiredSize;
    mFileMap[inName] = tmp;
    
    mMapFlags |= map_needs_rewrite; // Make sure we write out a new map with the extra entry.
            
    return mFileMap[inName];
}


bool    file_disk::write()
{
    if( mFileSize == 0 )
    {
        uint32_t    fileVersion = 0x00000101;
        mFile.seekp(0,ios::beg);
        mFile.write( (char*)&fileVersion, sizeof(fileVersion) );
        mFile.write( (char*)&mMapOffset, sizeof(mMapOffset) );
        mFileSize = sizeof(fileVersion) +sizeof(mMapOffset);
    }
    
    // Write out the data for all blocks, creating new ones
    //  as needed. While we're iterating, we also calculate
    //  the size we'll need for the map.
    size_t  mapSize = sizeof(uint64_t);
    
    // +++ to preserve file integrity in case of a full disk, we should really only write
    //  new and resized blocks below, and wait with writing out blocks where data just
    //  changed in place until we've written the map. After all, overwriting in-place is
    //  much less likely to fail, while growing so large the disk is full is quite common,
    //  so we'd only do some needless writing of free blocks, but keep a valid file.
    
    for( auto currNodeEntry : mFileMap )
    {
        file_node& currNode = currNodeEntry.second;
        mapSize += currNode.node_size_on_disk();
        
        if( (currNode.flags() & data_dirty) && currNode.name().size() != 0 )
        {
            if( currNode.logical_size() > currNode.physical_size() )
                swap_node_for_free_node_of_size( currNode, currNode.physical_size() );
            
            mFile.seekp( currNode.start_offset() );
            mFile.write( (char*) currNode.cached_data(), currNode.physical_size() );
            // Ensure we fill up the gap behind the block.
            //  +++ Should optimize this to not clear data if we already have old data
            //  in the file.
            for( uint64_t x = currNode.physical_size(); x < currNode.logical_size(); x++ )
            {
                uint8_t nullByte = 0;
                mFile.write( (char*)&nullByte, sizeof(nullByte) );
            }
            currNode.set_flags( currNode.flags() & ~data_dirty );
        }
    }
    
    if( (mMapFlags & map_needs_rewrite) )
    {
        file_node   dummy;
        dummy.set_name(MAP_BLOCK_FILENAME);
        file_node&  mapEntry = dummy;
        auto mapEntryItty = mFileMap.find(MAP_BLOCK_FILENAME);  // Have a map?
        if( mapEntryItty == mFileMap.end() )
        {
            mapEntry = node_of_size_for_name( mapSize, MAP_BLOCK_FILENAME, mapSize +mapEntry.node_size_on_disk() );
        }
        else
        {
            mapEntry = mapEntryItty->second;
            swap_node_for_free_node_of_size( mapEntry, mapSize, mapSize +mapEntry.node_size_on_disk() );
        }
        
        mMapOffset = mapEntry.start_offset();
        mFile.seekp( mMapOffset );
        uint64_t    numEntries = mFileMap.size() + mFreeBlocks.size();
        mFile.write( (char*)&numEntries, sizeof(numEntries) );
    }

    // +++ We should use a different collection that guarantees that
    //  items are in the same spots if their count/size hasn't changed,
    //  and ideally new items get moved where the last one was deleted.
    //  then we could just selectively overwrite parts of existing
    //  items instead of having to write out the entire map. Since we
    //  have no guarantee regarding order, the moment where we overwrite
    //  the map with its new state is easily the most dangerous moment
    //  where a power outage can corrupt your file.
    // Failing that, we could just treat map_needs_rewrite == 0 same
    //  as map_needs-rewrite == 1 and just write the updated Mac in
    //  a second location, giving us a copy of the map. Then the
    //  point where things can fail is only when we write the new
    //  map offset in, which is highly unlikely.
    mFile.seekp( mMapOffset +sizeof(uint64_t), ios::beg );
    for( auto currNodeEntry : mFileMap )
    {
        const file_node& currNode = currNodeEntry.second;
        currNode.write( mFile );
    }
    for( auto currNode : mFreeBlocks )
    {
        currNode.write( mFile );
    }
    
    mFile.seekp( sizeof(uint32_t), ios::beg );    // Seek past version number to map offset.
    mFile.write( (char*)&mMapOffset, sizeof(mMapOffset) );
    
    mMapFlags &= ~(map_needs_rewrite | offsets_dirty | data_dirty);
    
    return true;
}


bool    file_disk::add_file( const char* inFileName, char* inData, size_t dataSize, size_t blockSize )
{
    if( mFileSize == 0 )    // Totally new file?
    {
        if( !write() )   // Make sure we have a TOC and have calculated the file size.
            return false;
    }
    
    if( blockSize == 0 )
        blockSize = dataSize;
    
    if( mFileMap.find( inFileName ) != mFileMap.end() ) // File of this name already exists?
        return false;
    
    file_node&  newNode = node_of_size_for_name( blockSize, inFileName );
    newNode.set_logical_size( dataSize );
    if( inData )
    {
        newNode.set_cached_data( inData );
        newNode.set_flags( newNode.flags() | data_dirty );
        mMapFlags |= data_dirty;
    }
    
    return true;
}


bool    file_disk::set_file_contents( const char* inFileName, char* inData, size_t dataSize )
{
    if( inFileName[0] == 0 )
        return false; // Can't delete the file map.
    
    auto fileItty = mFileMap.find(inFileName);
    if( fileItty == mFileMap.end() )
        return false;
    
    if( dataSize > fileItty->second.physical_size() )
    {
        swap_node_for_free_node_of_size( fileItty->second, dataSize );
    }
    
    if( fileItty->second.cached_data() )
        delete [] fileItty->second.cached_data();
    fileItty->second.set_cached_data( inData );
    fileItty->second.set_logical_size( dataSize );
    fileItty->second.set_flags( fileItty->second.flags() | file_node::data_dirty | file_node::offsets_dirty );
    mMapFlags |= data_dirty;
    
    return true;
}


bool    file_disk::write( const char* buf, size_t numBytes, file_node& inFileNode )
{
    size_t dataSize = inFileNode.physical_size() + numBytes;
    if( dataSize > inFileNode.physical_size() )
    {
        swap_node_for_free_node_of_size( inFileNode, dataSize );
    }
    
    if( inFileNode.cached_data() )
        delete [] inFileNode.cached_data();
    inFileNode.set_cached_data( nullptr );
    inFileNode.set_logical_size( dataSize );
    inFileNode.set_flags( inFileNode.flags() | file_node::data_dirty | file_node::offsets_dirty );
    mMapFlags |= data_dirty;
    
    exit(1);    // +++ finish implementation, doesn't move data yet.
    
    return true;
}


bool    file_disk::read( char* buf, size_t numBytes, file_node& inFileNode )
{
    exit(1);    // +++ finish implementation.
    
    return true;
}


bool    file_disk::is_valid()
{
    bool                    foundMapBlock = false;
    index_set<uint64_t>     occupiedByteRanges;
    for( auto currNodeEntry : mFileMap )
    {
        const file_node& currNode = currNodeEntry.second;
        
        if( currNode.physical_size() < 1 )
            return false;
        if( currNode.physical_size() < currNode.logical_size() )
            return false;
        if( (currNode.start_offset() +currNode.physical_size()) > mFileSize )
            return false;
        if( currNode.start_offset() == mMapOffset )
            foundMapBlock = true;
        
        if( occupiedByteRanges.append( currNode.start_offset() +1, currNode.start_offset() + currNode.physical_size() ) != index_set<uint64_t>::does_not_exist )
            return false;   // Some blocks overlap :-o
    }
    
    if( !foundMapBlock )
        return false;
    
    for( auto currNode : mFreeBlocks )
    {
        if( currNode.physical_size() < currNode.logical_size() )
            return false;
        if( (currNode.start_offset() +currNode.physical_size()) > mFileSize )
            return false;
        
        if( occupiedByteRanges.append( currNode.start_offset() +1, currNode.start_offset() + currNode.physical_size() ) != index_set<uint64_t>::does_not_exist )
            return false;   // Some blocks overlap :-o
    }
    
    return true;
}


bool    file_disk::delete_file( const char* inFileName )
{
    if( inFileName[0] == 0 )
        return false; // Can't delete the file map.
    
    auto fileItty = mFileMap.find(inFileName);
    if( fileItty == mFileMap.end() )
        return false;
    
    // Get rid of RAM data for this node and
    //  move it to the free list:
    file_node&  nodeToDelete = fileItty->second;
    if( nodeToDelete.cached_data() )
    {
        delete [] nodeToDelete.cached_data();
        nodeToDelete.set_cached_data( nullptr );
    }
    nodeToDelete.set_flags( file_node::is_free );
    nodeToDelete.set_name("");
    mFreeBlocks.push_back( nodeToDelete );
    mFileMap.erase( fileItty );
    mMapFlags |= map_needs_rewrite;
    
    return true;
}


bool    file_disk::compact()
{
    // Generate a unique file name for the temp file in which we'll
    //  write the compacted version of our file:
    string      compactedPath(mFilePath);
    size_t      x = 1;
    while( true )
    {
        stringstream          uniquedPath;
        uniquedPath << compactedPath << "." << x;
        if( !ifstream(uniquedPath.str().c_str()).is_open() )  // File doesn't exist yet?
        {
            compactedPath = uniquedPath.str();
            break;
        }
        if( x == 0 )    // Number wrapped? Means we couldn't find a file name.
            return false;   // ABORT! ABORT!
        x++;
    }
    
    fstream                 compactedFile( compactedPath.c_str(), ios::binary | ios::out | ios::trunc );
    std::vector<file_node>  compactedBlocks;
    
    // Write file header (version & map offset):
    uint32_t        version = 0x00000101;
    uint64_t        mapOffset = sizeof(mapOffset) +sizeof(version);
    uint64_t        mapSize = sizeof(uint64_t);
    compactedFile.seekp(0, ios::beg);
    compactedFile.write( (char*)&version, sizeof(version) );
    compactedFile.write( (char*)&mapOffset, sizeof(mapOffset) );
    
    // Now loop over all blocks and write out their data. We create a
    //  second block map during this with the new offsets in it.
    //  We also calculate the size the map will need for these blocks
    //  and advance the mapOffset offset so it will point right after
    //  the last block's data.
    for( auto currNodeEntry : mFileMap )
    {
        if( currNodeEntry.second.name().compare(MAP_BLOCK_FILENAME) == 0 )    // Skip the map, we'll add a new one.
            continue;
        
        if( currNodeEntry.second.cached_data() != nullptr )
        {
            compactedFile.write( currNodeEntry.second.cached_data(), currNodeEntry.second.logical_size() );
            delete [] currNodeEntry.second.cached_data();
            currNodeEntry.second.set_cached_data( nullptr );
        }
        else
        {
            size_t  numBytes = currNodeEntry.second.logical_size();
            mFile.seekg( currNodeEntry.second.start_offset(), ios::beg );
            for( size_t x = 0; x < numBytes; x++ )
            {
                char    theCh = 0;
                mFile.read( &theCh, sizeof(theCh) );
                compactedFile.write( &theCh, sizeof(theCh) );
            }
        }
        
        file_node currNode = currNodeEntry.second;
        currNode.set_start_offset( mapOffset );
        currNode.set_physical_size( currNode.logical_size() );
        compactedBlocks.push_back( currNode );
        mapOffset += currNode.logical_size();
        mapSize += currNode.node_size_on_disk();
    }
    
    // Now build a node entry representing the area occupied by the map:
    file_node   mapNode;
    mapNode.set_name( MAP_BLOCK_FILENAME );
    mapSize += mapNode.node_size_on_disk(); // Apart from name, all other fields are constant length, so we can determine the size now and immediately assign it to mapNode's fields.
    mapNode.set_start_offset( mapOffset );
    mapNode.set_logical_size( mapSize );
    mapNode.set_physical_size( mapSize );
    compactedBlocks.push_back( mapNode );
    
    // Now write out the map as a count + node entries:
    uint64_t numBlocks = compactedBlocks.size();
    compactedFile.write( (char*) &numBlocks, sizeof(numBlocks) );
    for( const file_node& currNode : compactedBlocks )
    {
        currNode.write( compactedFile );
    }
    
    // Now write out the map offset:
    compactedFile.seekp( sizeof(uint32_t), ios::beg );
    compactedFile.write( (char*) &mapOffset, sizeof(mapOffset) );

    // Now close the old file and then delete it, then rename the new file
    //  to the old name:
    mFile.close();
    remove( mFilePath.c_str() );
    compactedFile.close();
    rename( compactedPath.c_str(), mFilePath.c_str() );
    
    mFileMap.clear();
    mFreeBlocks.clear();
    
    return open( mFilePath );
}


bool   file_disk::statistics( struct stats* outStatistics )
{
    memset( outStatistics, 0, sizeof(struct stats) );
    
    outStatistics->header_bytes = sizeof(uint32_t) +sizeof(uint64_t);
    
    for( auto currNodeEntry : mFileMap )
    {
        const file_node& currNode = currNodeEntry.second;
        if( (currNode.flags() & file_node::is_free) != 0 )
            cout << "Internal error: free block in used list." << endl;
        if( currNode.name().compare(MAP_BLOCK_FILENAME) == 0 )
        {
            outStatistics->map_bytes = currNode.logical_size();
        }
        else
        {
            outStatistics->used_bytes += currNode.logical_size();
            outStatistics->num_files ++;
        }
        outStatistics->name_bytes += currNode.name().size();
        outStatistics->free_bytes += currNode.physical_size() -currNode.logical_size();
    }
    for( auto currNode : mFreeBlocks )
    {
        if( (currNode.flags() & file_node::is_free) == 0 )
            cout << "Internal error: used block in free list." << endl;
        outStatistics->name_bytes += currNode.name().size();
        outStatistics->free_bytes += currNode.physical_size();
    }
    
    return true;
}

bool    file_node::read( std::iostream& inFile )
{
    uint8_t     nameLen = 0;
    
    inFile.read( (char*)&nameLen, sizeof(nameLen) );
    char        name[256] = {0};
    inFile.read( name, nameLen );
    inFile.read( (char*)&mStartOffs, sizeof(mStartOffs) );
    inFile.read( (char*)&mLogicalSize, sizeof(mLogicalSize) );
    inFile.read( (char*)&mPhysicalSize, sizeof(mPhysicalSize) );
    inFile.read( (char*)&mFlags, sizeof(mFlags) );
    if( (mFlags & is_free) == 0 )
        mName = name;   // Don't bother keeping around file names of free blocks, there shouldn't be any.
    mFlags &= ~(data_dirty | offsets_dirty | name_dirty);
    
    return true;
}


bool    file_node::write( std::iostream& inFile ) const
{
    uint8_t     nameLen = mName.size();
    inFile.write( (char*)&nameLen, sizeof(nameLen) );
    inFile.write( mName.c_str(), nameLen );
    inFile.write( (char*)&mStartOffs, sizeof(mStartOffs) );
    inFile.write( (char*)&mLogicalSize, sizeof(mLogicalSize) );
    inFile.write( (char*)&mPhysicalSize, sizeof(mPhysicalSize) );
    node_flags_t    flags = mFlags & ~(data_dirty | offsets_dirty | name_dirty);
    inFile.write( (char*)&flags, sizeof(mFlags) );
    
    return true;
}


} /* namespace file_disk */
