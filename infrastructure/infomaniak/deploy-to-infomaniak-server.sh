#!/bin/bash
set -e

SERVER=deploy@179.237.68.137
REMOTE_DIR=/home/deploy/horse-collar

echo "=== Pre-Deployment Checks ==="

# Check for uncommitted changes
if ! git diff-index --quiet HEAD --; then
    echo "WARNING: You have uncommitted changes:"
    git status --short
    read -p "Continue anyway? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Deployment cancelled."
        exit 0
    fi
fi

# Get current deployment info
current_branch=$(git rev-parse --abbrev-ref HEAD)
commit_hash=$(git rev-parse HEAD)
commit_short=$(git rev-parse --short HEAD)
commit_message=$(git log -1 --pretty=%B)

echo ""
echo "Current branch: $current_branch"
echo "Commit: $commit_short - $commit_message"

# Check what's currently deployed
echo ""
echo "Checking current deployment on server..."
current_deployment=$(ssh $SERVER "cat $REMOTE_DIR/deployment.txt 2>/dev/null || echo 'No deployment info found'")
if [ "$current_deployment" != "No deployment info found" ]; then
    echo "Currently deployed:"
    echo "$current_deployment"
else
    echo "No previous deployment info found on server"
fi

# Create deployment tracking
timestamp=$(date +"%Y-%m-%d_%H-%M-%S")
deployment_branch="deployment/$timestamp"
deployment_tag="deploy-$timestamp"

echo ""
echo "Creating deployment tracking:"
echo "  Branch: $deployment_branch"
echo "  Tag: $deployment_tag"

git branch $deployment_branch
git tag -a $deployment_tag -m "Deployment at $timestamp from $current_branch ($commit_short)"

# Create deployment info file
cat > deployment.txt << EOF
Deployment Date: $timestamp
Branch: $current_branch
Commit: $commit_hash
Commit Short: $commit_short
Message: $commit_message
Deployed By: $USER
Deployed From: $(hostname)
EOF

echo ""
echo "Deployment info created"
echo ""
echo "Building Docker images..."

docker compose --file docker-compose.build.yml build

docker save horse-collar-webapi | bzip2 | pv | ssh $SERVER docker load
docker save horse-collar-svelte  | bzip2 | pv | ssh $SERVER docker load

scp docker-compose.remote.yml $SERVER:$REMOTE_DIR
scp .env                       $SERVER:$REMOTE_DIR
scp deployment.txt             $SERVER:$REMOTE_DIR

ssh $SERVER << EOF
    cd $REMOTE_DIR
    docker compose --file docker-compose.remote.yml down
    docker compose --file docker-compose.remote.yml up -d
    docker system prune -f
EOF

echo ""
echo "Deployment completed successfully!"
echo "Deployment tracked in:"
echo "  Branch: $deployment_branch"
echo "  Tag: $deployment_tag"
echo "  Commit: $commit_short"
echo ""
echo "Cleaning up..."
rm -f deployment.txt

git checkout $current_branch

echo ""
echo "To check what's deployed on server, run:"
echo "  ssh $SERVER cat $REMOTE_DIR/deployment.txt"
echo ""
echo "To view deployment history:"
echo "  git branch --list 'deployment/*'"
echo "  git tag --list 'deploy-*'"
