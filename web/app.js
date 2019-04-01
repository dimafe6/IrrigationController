var ws;

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
            if (jsonObject.command) {

            }
        };

        ws.onclose = function () {
            console.log("WS is closed...");
        };
    } else {
        alert("WebSocket NOT supported by your Browser!");
    }
}

function createZonesSelector($element) {
    $($element).append('<div class="btn-group" data-toggle="buttons">\n' +
        '<label class="btn btn-default active">\n' +
        '    <input type="checkbox" id="zone1">Zone 1\n' +
        '</label>\n' +
        '<label class="btn btn-default">\n' +
        '    <input type="checkbox" id="zone2">Zone 2\n' +
        '</label>\n' +
        '<label class="btn btn-default">\n' +
        '    <input type="checkbox" id="zone3">Zone 3\n' +
        '</label>\n' +
        '<label class="btn btn-default">\n' +
        '    <input type="checkbox" id="zone4">Zone 4\n' +
        '</label>\n' +
        '</div>');
}