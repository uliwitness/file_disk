//
//  file_disk.cpp
//  FileDisk
//
//  Created by Uli Kusterer on 09/08/15.
//  Copyright (c) 2015 Uli Kusterer. All rights reserved.
//

#include "file_disk.h"
#include <iostream>


using namespace std;


file_disk::file_disk( const std::string& inPath )
    : mVersion(0x00000101), mMapOffset(0), mMapFlags(0)
{
    mFile.open( inPath.c_str(), ios::binary | ios::in | ios::out );
    if( !mFile.is_open() )  // File doesn't exist?
        mFile.open( inPath.c_str(), ios::binary | ios::in | ios::out | ios::trunc );
    mFile.seekg( 0, ios::end );
    mFileSize = mFile.tellg();
    if( mFileSize == 0 )
        mMapFlags = map_needs_rewrite | offsets_dirty | data_dirty;
    
    if( !load_map() )
        cout << "Failed to open file." << endl;
}


file_disk::~file_disk()
{
    
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
            mMapFlags |= map_needs_rewrite;
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
    mFileSize += desiredSize;
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
            mapEntry = node_of_size_for_name( mapSize, MAP_BLOCK_FILENAME, mapSize +mapEntry.node_size_on_disk() );
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
        nodeToDelete.set_flags( file_node::is_free );
        nodeToDelete.set_name("");
        mFreeBlocks.push_back( nodeToDelete );
        mFileMap.erase( fileItty );
        mMapFlags |= map_needs_rewrite;
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
