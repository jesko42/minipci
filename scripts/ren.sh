#!/bin/sh

git filter-branch -f --env-filter '
OLD_EMAIL="minipci@schwarzers.de"
CORRECT_NAME="jesko42"
CORRECT_EMAIL="github.com@schwarzers.de"
if [ "$GIT_COMMITTER_EMAIL" = "$OLD_EMAIL" ]
then
    export GIT_COMMITTER_NAME="$CORRECT_NAME"
    export GIT_COMMITTER_EMAIL="$CORRECT_EMAIL"
fi
if [ "$GIT_AUTHOR_EMAIL" = "$OLD_EMAIL" ]
then
    export GIT_AUTHOR_NAME="$CORRECT_NAME"
    export GIT_AUTHOR_EMAIL="$CORRECT_EMAIL"
fi
' --tag-name-filter cat -- --branches --tags


# check changes

# afterwards:
git push --force --tags origin 'refs/heads/*'
