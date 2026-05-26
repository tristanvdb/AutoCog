#!/usr/bin/env python3
"""
Test harness for parsing all .sta files in a directory tree.
Usage: python tests/parse_sta_files.py [directory]
If no directory is specified, uses current directory.
"""

import os
import sys
import argparse
from pathlib import Path
from autocog.sta.frontend import frontend


def find_sta_files(directory):
    """Recursively find all .sta files in the given directory."""
    sta_files = []
    
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith('.sta'):
                sta_files.append(Path(root) / file)
    
    return sorted(sta_files)


def parse_sta_file(filepath):
    """
    Parse a single .sta file and return success status with error details.
    
    Returns:
        tuple: (success: bool, error_msg: str or None)
    """
    try:
        with open(filepath, 'r') as f:
            content = f.read()
        
        # Parse the STA file
        ast = frontend(content)
        return (True, None)
        
    except FileNotFoundError:
        return (False, f"File not found: {filepath}")
    except Exception as e:
        # Get the error type and message
        error_type = type(e).__name__
        error_msg = str(e)
        
        # Try to extract line number if available in the error
        if hasattr(e, 'line'):
            return (False, f"{error_type} at line {e.line}: {error_msg}")
        else:
            return (False, f"{error_type}: {error_msg}")


def get_relative_path(filepath, base_dir):
    """Get a nice relative path for display."""
    try:
        return filepath.relative_to(base_dir)
    except ValueError:
        # If filepath is not relative to base_dir, return absolute path
        return filepath


def print_file_snippet(filepath, max_lines=5):
    """Print the first few lines of a file for context."""
    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()[:max_lines]
        
        print("  File content (first {} lines):".format(min(len(lines), max_lines)))
        for i, line in enumerate(lines, 1):
            print(f"    {i:3}: {line.rstrip()}")
        
        if len(lines) == max_lines:
            print(f"    ... ({filepath.stat().st_size} bytes total)")
    except Exception as e:
        print(f"  Could not read file content: {e}")


def main():
    # Set up argument parser
    parser = argparse.ArgumentParser(
        description='Parse all .sta files in a directory tree to validate syntax.'
    )
    parser.add_argument(
        'directory',
        nargs='?',
        default='.',
        help='Directory to search for .sta files (default: current directory)'
    )
    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help='Show file snippets for failed parses'
    )
    parser.add_argument(
        '--quiet', '-q',
        action='store_true',
        help='Only show summary, not individual file results'
    )
    parser.add_argument(
        '--fail-fast', '-f',
        action='store_true',
        help='Stop on first parse failure'
    )
    
    args = parser.parse_args()
    
    # Convert directory to Path object
    search_dir = Path(args.directory).resolve()
    
    if not search_dir.exists():
        print(f"Error: Directory '{search_dir}' does not exist")
        sys.exit(1)
    
    if not search_dir.is_dir():
        print(f"Error: '{search_dir}' is not a directory")
        sys.exit(1)
    
    # Header
    print("=" * 70)
    print("AutoCog STA File Parser Test Harness")
    print("=" * 70)
    print(f"Searching for .sta files in: {search_dir}")
    print()
    
    # Find all .sta files
    sta_files = find_sta_files(search_dir)
    
    if not sta_files:
        print("No .sta files found in the specified directory")
        sys.exit(0)
    
    print(f"Found {len(sta_files)} .sta file(s)")
    print("-" * 70)
    print()
    
    # Process each file
    passed = 0
    failed = 0
    failed_files = []
    
    for filepath in sta_files:
        relative_path = get_relative_path(filepath, search_dir)
        
        # Parse the file
        success, error_msg = parse_sta_file(filepath)
        
        if success:
            passed += 1
            if not args.quiet:
                print(f"✓ {relative_path}")
        else:
            failed += 1
            failed_files.append((relative_path, error_msg))
            
            if not args.quiet:
                print(f"✗ {relative_path}")
                print(f"  Error: {error_msg}")
                
                if args.verbose:
                    print_file_snippet(filepath)
                
                print()
            
            if args.fail_fast:
                print("\nStopping due to --fail-fast flag")
                break
    
    # Summary
    print()
    print("=" * 70)
    print("Summary")
    print("=" * 70)
    print(f"Total files:  {len(sta_files)}")
    print(f"Passed:       {passed} ({passed/len(sta_files)*100:.1f}%)")
    print(f"Failed:       {failed} ({failed/len(sta_files)*100:.1f}%)")
    
    if failed > 0 and not args.quiet:
        print()
        print("Failed files:")
        for filepath, error_msg in failed_files:
            print(f"  - {filepath}")
            if args.verbose:
                print(f"    {error_msg}")
    
    print("=" * 70)
    
    # Exit with appropriate code
    if failed > 0:
        sys.exit(1)
    else:
        print("\nAll .sta files parsed successfully!")
        sys.exit(0)


if __name__ == "__main__":
    main()
