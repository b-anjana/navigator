echo OFF
set HOME=%userprofile%
echo Starting Docker Compose config...
docker-compose up -d

echo Starting interactive shell
docker-compose exec -u docker base bash