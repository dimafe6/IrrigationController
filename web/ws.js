var ws;

        function mouseDown() {
            ws.send('{"led" : 1}');
        }

        function mouseUp() {
            ws.send('{"led" : 0}');
        }

        function WebSocketBegin() {
            if ("WebSocket" in window) {
                ws = new WebSocket("ws://" + location.hostname + "/ws");
                ws.onopen = function () {
                    console.log("WS connected");
                    //ws.send('{"command" : "getHeap"}');
                };

                ws.onmessage = function (evt) {
                    var jsonObject = JSON.parse(evt.data);
                    console.log(jsonObject);
                    if(jsonObject.command) {

                    }
                };

                ws.onclose = function () {
                    console.log("WS is closed...");
                };
            } else {
                alert("WebSocket NOT supported by your Browser!");
            }
        }