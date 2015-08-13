//
//  index_set.h
//  FileDisk
//
//  Created by Uli Kusterer on 11/08/15.
//  Copyright (c) 2015 Uli Kusterer. All rights reserved.
//

#ifndef __FileDisk__index_set__
#define __FileDisk__index_set__

#include <vector>
#include <iostream>


namespace fld
{

template<class Integer>
class index_set
{
public:
    enum existence { does_not_exist, partially_exists, fully_exists };
    
    enum existence    append( Integer a )    { return append( a, a ); };
    enum existence    append( Integer lowest, Integer highest );
    
    enum existence    has( Integer a )       { return has( a, a ); };
    enum existence    has( Integer lowest, Integer highest );
 
    void    print( std::ostream& outStream );
    
protected:
    struct range
    {
        Integer      start;
        Integer      end;
    };
    
    std::vector<range>     mIntegers;
};

template<class Integer>
enum index_set<Integer>::existence    index_set<Integer>::append( Integer lowest, Integer highest )
{
    for( auto currRangeItty = mIntegers.begin(); currRangeItty != mIntegers.end(); currRangeItty++ )
    {
        if( currRangeItty->start <= lowest && currRangeItty->end >= highest )
        {
            return fully_exists;   // Already have this range, nothing to add.
        }
        else if( currRangeItty->start > highest )   // Didn't match a previous one, and the next one is beyond our end?
        {
            mIntegers.insert( currRangeItty, (range){ lowest, highest } );   // Add new entry for this.
            return does_not_exist;
        }
        else if( currRangeItty->end == (lowest -1) )    // Follows immediately after this one?
        {
            auto prevRangeItty = currRangeItty;
            currRangeItty->end = highest;   // Just extend the range.
            ++currRangeItty;
            enum existence result = does_not_exist;
            
            while( currRangeItty != mIntegers.end() && currRangeItty->start <= (highest+1) )   // We just closed the gap to the next one?
            {
                if( currRangeItty->end > prevRangeItty->end )
                {
                    prevRangeItty->end = currRangeItty->end;    // Merge the next one into this range.
                    if( currRangeItty->start <= highest )
                        result = partially_exists;
                }
                else if( currRangeItty->start <= highest )
                    result = fully_exists;
                currRangeItty = mIntegers.erase(currRangeItty);  // And delete the next one.
            }
            return result;   // The values only abut, we didn't have any overlap, so this is not a partial add.
        }
        else if( currRangeItty->start == (highest +1) )    // Immediately precedes this one?
        {
            currRangeItty->start = lowest;   // Just extend the range.
            return does_not_exist;   // The values only abut, we didn't have any overlap, so this is not a partial add.
        }
        else if( currRangeItty->start <= lowest && currRangeItty->end <= highest
                && lowest <= currRangeItty->end )   // We overlap at our start with the current range?
        {
            auto prevRangeItty = currRangeItty;
            currRangeItty->end = highest;   // Just extend the range.
            ++currRangeItty;
            
            while( currRangeItty != mIntegers.end() && currRangeItty->start <= (highest+1) )   // We just closed the gap to the next one?
            {
                if( currRangeItty->end > prevRangeItty->end )
                {
                    prevRangeItty->end = currRangeItty->end;    // Merge the next one into this range.
                }
                currRangeItty = mIntegers.erase(currRangeItty);  // And delete the next one.
            }
            return partially_exists;   // The values only abut, we didn't have any overlap, so this is not a partial add.
        }
        else if( currRangeItty->start >= lowest && currRangeItty->end >= highest
                && highest >= currRangeItty->start)   // We only overlap at our end with the current range?
        {
            currRangeItty->start = lowest;
            return partially_exists;
        }
        else if( lowest < currRangeItty->start && highest > currRangeItty->end )   // This is a subset in the middle of our range.
        {
            currRangeItty->start = lowest;
            currRangeItty->end = highest;
            auto prevRangeItty = currRangeItty;
            ++currRangeItty;
            
            while( currRangeItty != mIntegers.end() && currRangeItty->start <= (highest+1) )   // We just closed the gap to the next one?
            {
                if( currRangeItty->end > prevRangeItty->end )
                {
                    prevRangeItty->end = currRangeItty->end;    // Merge the next one into this range.
                }
                currRangeItty = mIntegers.erase(currRangeItty);  // And delete the next one.
            }
            return partially_exists;   // The values only abut, we didn't have any overlap, so this is not a partial add.
        }
    }
    
    // If we get here, it's the highest value yet, and doesn't overlap with or abut any other value.
    mIntegers.insert( mIntegers.end(), (range){ lowest, highest } );
    return does_not_exist;
}


template<class Integer>
enum index_set<Integer>::existence    index_set<Integer>::has( Integer lowest, Integer highest )
{
    for( const range& currRange : mIntegers )
    {
        if( currRange.start <= lowest && currRange.end >= highest )
        {
            return fully_exists;
        }
        else if( currRange.start > highest )   // Didn't match a previous one, and the next one is beyond our end?
        {
            return does_not_exist;
        }
        else if( currRange.start <= lowest && currRange.end <= highest
                && lowest <= currRange.end )   // We overlap at our start with the current range?
        {
            return partially_exists;
        }
        else if( currRange.start >= lowest && currRange.end >= highest
                && highest >= currRange.start)   // We only overlap at our end with the current range?
        {
            return partially_exists;
        }
    }

    return does_not_exist;
}


template<class Integer>
void    index_set<Integer>::print( std::ostream &outStream )
{
    outStream << mIntegers.size() << " ranges:" << std::endl;
    for( const range& currRange : mIntegers )
    {
        outStream << "{ " << currRange.start << ", " << currRange.end << " }" << std::endl;
    }
}

}

#endif /* defined(__FileDisk__index_set__) */
