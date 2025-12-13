#!/bin/sh

filesdir=$1
searchstr=$2

if [[ -z $filesdir || -z $searchstr ]]; then
    echo "Search directory or search string or both were not specified"
    exit 1
fi

if [[ ! -d $filesdir ]]; then
    echo "$filesdir is not a directory"
    exit 1
fi

file_count=$(find $filesdir -type f | wc -l)
# make sure to ignore binary files, -I
match_line_count=$(find $filesdir -type f -exec grep -I $searchstr {} + | wc -l)

echo "The number of files are $file_count and the number of matching lines are $match_line_count"
