 g++ -o server wsServer.cpp


  python3 -m http.server 8000



  http://localhost:8000/server.html

sqlite installation: sudo apt-get install libsqlite3-dev
  if error with sqlite during compilation , compile it as: g++ wsServer.cpp -o wsServer -lsqlite3