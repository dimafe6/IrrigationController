var ws;

$(document).ready(function () {
    WebSocketBegin();
});

function WebSocketBegin() {
    if ("WebSocket" in window) {
        ws = new WebSocket("ws://" + location.hostname + "/ws");
        ws.onopen = function () {
            console.log("WS connected");
        };

        ws.onmessage = function (evt) {
            var jsonObject = JSON.parse(evt.data);
            console.log(jsonObject);
            var command = jsonObject.command || null;
            if (null !== command) {
                var data = jsonObject.data || null;
                var status = jsonObject.status || false;
                switch (command) {
                    case 'manualIrrigation':
                        if (status) {
                            $('#manual-mode .stop-irrigation-btn').show();
                            $('#manual-mode .start-irrigation-btn').hide();
                            console.log('Irrigation started');
                        } else {
                            $('#manual-mode .stop-irrigation-btn').hide();
                            $('#manual-mode .start-irrigation-btn').show();
                            console.log('Irrigation has not started. Try again');
                        }
                        break;
                    case 'stopManualIrrigation':
                        $('#manual-mode .stop-irrigation-btn').hide();
                        $('#manual-mode .start-irrigation-btn').show();
                        console.log('Irrigation stopped');
                        break;
                }
            }
        };

        ws.onclose = function () {
            console.log("WS is closed...");
        };
    } else {
        alert("WebSocket NOT supported by your Browser!");
    }
}

function manualIrrigation() {
    if ($('#manual-mode-zones .btn-default.active').length === 0) {
        alert("Select minimum one zone!");
        $('#manual-mode-zones input[type="checkbox"]:eq(0)').click();
        return;
    }
    var command = {};
    command.command = "manualIrrigation";
    command.data = {};
    var checked = []
    $('#manual-mode-zones input[type="checkbox"]').each(function () {
        if ($(this).parent().hasClass('active')) {
            checked.push(parseInt($(this).val()));
        }
    });
    command.data.duration = parseInt($('#duration').val());
    command.data.zones = checked;

    ws.send(JSON.stringify(command));
}

function stopManualIrrigation() {
    ws.send('{"command": "stopManualIrrigation"}');
    $('#manual-mode .stop-irrigation-btn').hide();
    $('#manual-mode .start-irrigation-btn').show();
}