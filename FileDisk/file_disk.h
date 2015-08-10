//
//  file_disk.h
//  FileDisk
//
//  Created by Uli Kusterer on 09/08/15.
//  Copyright (c) 2015 Uli Kusterer. All rights reserved.
//

#ifndef __FileDisk__file_disk__
#define __FileDisk__file_disk__

#include <stdio.h>
#include <string>
#include <fstream>
#include <map>
#include <vector>


namespace fld
{

extern const char* MAP_BLOCK_FILENAME;  // File name we give the map block in the map. This is an invalid file name, so should be fine.


class file_node
{
public:
    enum
    {
        is_free = (1 << 0),         // Flag on unused blocks that are available for reuse.
        name_dirty = (1 << 1),      // Name of a node changed, need to write a new map. (Not written to disk)
        offsets_dirty = (1 << 2),   // Only offsets/sizes/flags changed, can update map in-place. (Not written to disk)
        data_dirty = (1 << 3)       // Data changed or is new, write mCachedData to a free block or add a block to the end. (Not written to disk)
    };
    typedef uint32_t   node_flags_t;
    
    file_node() : mFlags(0), mStartOffs(0), mLogicalSize(0), mPhysicalSize(0), mCachedData(nullptr) {}
    file_node( const file_node& inOriginal ) : mFlags(inOriginal.mFlags), mStartOffs(inOriginal.mStartOffs), mLogicalSize(inOriginal.mLogicalSize), mPhysicalSize(inOriginal.mPhysicalSize), mCachedData(nullptr), mName(inOriginal.mName) { if( inOriginal.mCachedData != nullptr ) { mCachedData = new char[inOriginal.mLogicalSize]; memcpy(mCachedData, inOriginal.mCachedData, inOriginal.mLogicalSize); } }
//    file_node( file_node&& inOriginal ) : mFlags(inOriginal.mFlags), mStartOffs(inOriginal.mStartOffs), mLogicalSize(inOriginal.mLogicalSize), mPhysicalSize(inOriginal.mPhysicalSize), mCachedData(inOriginal.mCachedData), mName(inOriginal.mName) { inOriginal.mCachedData = nullptr; }
    ~file_node()    { if( mCachedData ) delete [] mCachedData; }
    
    bool    read( std::iostream& inFile );
    bool    write( std::iostream& inFile ) const;
    
    std::string     name() const                            { return mName; }
    void            set_name( const std::string &inString ) { mName = inString; }
    node_flags_t    flags() const                           { return mFlags; }
    void            set_flags( node_flags_t inFlags )       { mFlags = inFlags; }
    size_t          node_size_on_disk() const               { return mName.size() +1 +sizeof(mStartOffs) +sizeof(mLogicalSize) +sizeof(mPhysicalSize) +sizeof(mFlags); }
    size_t          start_offset() const                    { return mStartOffs; }
    void            set_start_offset( size_t inSize )       { mStartOffs = inSize; }
    size_t          logical_size() const                    { return mLogicalSize; }
    void            set_logical_size( size_t inSize )       { mLogicalSize = inSize; }
    size_t          physical_size() const                   { return mPhysicalSize; }
    void            set_physical_size( size_t inSize )      { mPhysicalSize = inSize; }
    const char*     cached_data() const                     { return mCachedData; }
    char*           cached_data()                           { return mCachedData; }
    void            set_cached_data( char* inData )         { mCachedData = inData; }   // Node takes ownership of data passed in, but caller must free previous data in mCachedData.
    
protected:
    std::string     mName;          // Name of the block (i.e. file-in-file). Max. 255 bytes.
    uint64_t        mStartOffs;     // Start offset into file where this block's data begins.
    uint64_t        mLogicalSize;   // Actual used amount of bytes on disk, or of mCachedData if data_dirty.
    uint64_t        mPhysicalSize;  // Number of bytes the block occupies on disk.
    node_flags_t    mFlags;         // Flags to save to file.
    char*           mCachedData;    // Cached data, an array of chars allocated using new.
};


struct stats
{
    uint64_t    used_bytes;     // How many bytes in file actually used for data.
    uint64_t    free_bytes;     // How many bytes in file unused & available for re-use.
    uint64_t    map_bytes;      // How many bytes in file used for map.
    uint64_t    header_bytes; // How many bytes in file used for version, map offset.
    uint64_t    name_bytes;     // How many bytes in file map used for names (excl. length bytes).
    uint64_t    num_files;      // How many files inside this file_disk.
};

class file_disk
{
public:
    enum
    {
        map_needs_rewrite = (1 << 0),   // Node name changed or nodes added/deleted.
        offsets_dirty = (1 << 1),       // Only offsets/sizes/flags changed, can update map in-place.
        data_dirty = (1 << 2)           // Data changed, write mCachedData to some block offsets.
    };
    typedef uint32_t   map_flags_t;
    

    file_disk();
    ~file_disk();
    
    bool            open( const std::string& inPath );
    bool            write();    // Commit all changes to this file to disk.
    bool            compact();


    // blockSize is the size of the actual block you want, e.g. if you want to reserve some room for growth
    //  in the block, but only have a bit of data to fill right now. If blockSize == 0, the block will be
    //  made dataSize bytes large (which is the mLogicalSize). Takes over ownership of the malloc()ed block
    //  inData. You may specify NULL for inData if dataSize == 0 and blockSize > 0.
    bool            add_file( const char* inFileName, char* inData, size_t dataSize, size_t blockSize = 0 );

    bool            delete_file( const char* inFileName );
    
    bool            statistics( struct stats* outStatistics );

protected:
    bool            load_map();
    void            swap_node_for_free_node_of_size( file_node& ioNode, size_t desiredSize, size_t desiredSizeIfNotRecycled = 0 );
    file_node&      node_of_size_for_name( size_t desiredSize, const std::string& inName, size_t desiredSizeIfNotRecycled = 0 );

protected:
    size_t                          mFileSize;  // Size in bytes of the file/position at which we append new blocks.
    std::map<std::string,file_node> mFileMap;   // List of used blocks in the file, indexed by name.
    map_flags_t                     mMapFlags;  // Whenever we set dirty flags, we also set them here, so that we know on save whether we need to write a new map, update it etc (Not written to disk).
    std::vector<file_node>          mFreeBlocks;// List of unused blocks in the file that we can re-use.
    std::fstream                    mFile;      // The actual binary file on disk where data is kept/persisted.
    std::string                     mFilePath;  // The path corresponding to mFile.
    uint32_t                        mVersion;   // Only low 2 bytes used for major/minor file format version. Other 2 bytes used to detect endian-ness (If high byte is not 0, you're reading an (currently unsupported) non-native endian file).
    uint64_t                        mMapOffset; // Position of the block that contains the block map.
};

} /* namespace file_disk*/

#endif /* defined(__FileDisk__file_disk__) */
