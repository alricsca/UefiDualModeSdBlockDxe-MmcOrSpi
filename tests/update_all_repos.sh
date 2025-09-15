#!/bin/bash

# Find all directories that are Git repositories and have submodules
find . -type d -name ".git" | while read git_dir; do
  # Get the parent directory (the root of the project)
  repo_dir=$(dirname "$git_dir")

  # Check if the repository has a .gitmodules file
  if [ -f "$repo_dir/.gitmodules" ]; then
    echo "Updating submodules in: $repo_dir"

    # Change into the repository directory
    cd "$repo_dir"

    # Run the submodule update command
    git submodule update --init --recursive

    # Change back to the starting directory
    cd - > /dev/null
  fi
done

echo "All submodule updates are complete."
