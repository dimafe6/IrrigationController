var websocketServerLocation = "ws://" + location.hostname + "/ws";
var ws;
var weekNames = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thuesday", "Friday", "Saturday"];

window.addEventListener('beforeunload', (event) => {
    ws.close();
});

$(document).ready(function () {
    moment.updateLocale('en', {
        week: {
            dow: 0,
            doy: 6
        }
    });

    WebSocketBegin(websocketServerLocation);

    $('.time-minute, .time-second').pickatime({
        format: 'i',
        interval: 1,
        min: [0, 0],
        max: [0, 59],
        clear: '',
        onClose: getSchedule
    });

    $('.time-hour-minute').pickatime({
        format: 'HH:i',
        interval: 15,
        clear: '',
        onClose: getSchedule
    }).pickatime('picker').set('select', 0);

    $('.time-hours').pickatime({
        format: 'HH',
        interval: 60,
        min: [1, 0],
        max: [23, 0],
        clear: '',
        onClose: getSchedule
    }).pickatime('picker').set('select', 1);

    $('.time-day-of-month').pickatime({
        format: 'i',
        interval: 1,
        min: [0, 1],
        max: [0, 28], //Minimum days for each months
        clear: '',
        onClose: getSchedule
    }).pickatime('picker').set('select', 1);

    $('.time-minute, .time-second, .time-hour-minute, .time-hours, .time-day-of-month').each(function () {
        $(this).pickatime('picker').set('select', 0);
    });

    $('#periodicity').change(function () {
        switch (parseInt($(this).val())) {
            case 0:
                $('.period-block ').hide();
                $('.period-block.hourly').show();
                break;
            case 1:
                $('.period-block ').hide();
                $('.period-block.every-x-hours').show();
                break;
            case 2:
                $('.period-block ').hide();
                $('.period-block.daily').show();
                break;
            case 3:
                $('.period-block ').hide();
                $('.period-block.every-x-days').show();
                break;
            case 4:
                $('.period-block ').hide();
                $('.period-block.weekly').show();
                break;
            case 5:
                $('.period-block ').hide();
                $('.period-block.monthly').show();
                break;
            default:
                $('.period-block ').hide();
                break;
        }

        getSchedule();
    });

    $('#periodicity').change();

    $('#schedule-mode-zones input[type="checkbox"]').change(getSchedule);
    $('.time-days').change(getSchedule);
    $('#weekdays-selector input').change(getSchedule);
    $('#schedule-mode .duration').change(getSchedule);
});

function notify(text, type) {
    $.notify(text, { type: type, placement: { from: 'top', align: 'center' } });
}

function WebSocketBegin(location) {
    if ("WebSocket" in window) {
        ws = new WebSocket(location);
        ws.onopen = function () {
            notify('WS connected', 'success');
            $('#ws-n-conn').hide();
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
                        $('#manual-mode .stop-irrigation-btn').show();
                        $('#manual-mode .start-irrigation-btn').hide();
                        break;
                    case 'stopManualIrrigation':
                        $('#manual-mode .stop-irrigation-btn').hide();
                        $('#manual-mode .start-irrigation-btn').show();
                        notify('Manual irrigation finished', 'success');
                        break;
                    case 'addSchedule':
                        notify('Schedule added', 'success');
                        $('.add-schedule').show();
                        break;
                    case 'getEvents':
                        console.log("getEvents");
                        break;
                    case 'removeEvent':
                        console.log("removeEvent");
                        break;
                }
            }
        };

        ws.onclose = function () {
            $('#ws-n-conn').show();
            setTimeout(function(){WebSocketBegin(websocketServerLocation)}, 5000);
        };

        ws.onerror = function (error) {
            if (error.message !== undefined) {
                notify('WS error: ' + error.message, 'danger');
            }
            setTimeout(function(){WebSocketBegin(websocketServerLocation)}, 5000);
        };
    } else {
        notify('WebSocket NOT supported by your Browser!', 'danger');
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

function getSchedule() {
    var $scheduleBlock = $('#schedule-mode');
    var eventSlot = {};
    var checked = []
    $('#schedule-mode-zones input[type="checkbox"]').each(function () {
        if ($(this).parent().hasClass('active')) {
            checked.push(parseInt($(this).val()));
        }
    });

    eventSlot.duration = parseInt($scheduleBlock.find('.duration').val());
    eventSlot.zones = checked;

    switch (parseInt($('#periodicity').val())) {
        case 0:
            var $periodBlock = $('.period-block.hourly');
            eventSlot.minute = parseInt($periodBlock.find('.time-minute').val());
            eventSlot.second = parseInt($periodBlock.find('.time-second').val());
            eventSlot.periodicity = 0;

            if (eventSlot.duration <= 0 || eventSlot.duration >= 59) {
                $scheduleBlock.find('.duration').val(59);
                eventSlot.duration = 59;
            }
            break;
        case 1:
            var $periodBlock = $('.period-block.every-x-hours');
            eventSlot.hours = parseInt($periodBlock.find('.time-hours').val());
            eventSlot.minute = parseInt($periodBlock.find('.time-minute').val());
            eventSlot.second = parseInt($periodBlock.find('.time-second').val());
            eventSlot.periodicity = 1;

            var maxDuration = (eventSlot.hours * 60) - 1;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 2:
            var time = $('.period-block.daily').find('.time-hour-minute').val();
            var timeArray = time.split(":");
            eventSlot.hour = parseInt(timeArray[0]);
            eventSlot.minute = parseInt(timeArray[1]);
            eventSlot.periodicity = 2;

            var maxDuration = 23 * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 3:
            var $periodBlock = $('.period-block.every-x-days');
            var time = $periodBlock.find('.time-hour-minute').val();
            var timeArray = time.split(":");
            eventSlot.days = parseInt($periodBlock.find('.time-days').val());
            eventSlot.hour = parseInt(timeArray[0]);
            eventSlot.minute = parseInt(timeArray[1]);
            eventSlot.periodicity = 3;

            var maxDuration = ((eventSlot.days * 24) - 1) * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 4:
            var $periodBlock = $('.period-block.weekly');
            var time = $periodBlock.find('.time-hour-minute').val();
            var timeArray = time.split(":");
            eventSlot.dayOfWeek = parseInt($('#weekdays-selector label.active').find('input').val());
            eventSlot.hour = parseInt(timeArray[0]);
            eventSlot.minute = parseInt(timeArray[1]);
            eventSlot.periodicity = 4;

            var maxDuration = ((7 * 24) - 1) * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 5:
            var $periodBlock = $('.period-block.monthly');
            var time = $periodBlock.find('.time-hour-minute').val();
            var timeArray = time.split(":");
            eventSlot.dayOfMonth = parseInt($periodBlock.find('.time-day-of-month').val());
            eventSlot.hour = parseInt(timeArray[0]);
            eventSlot.minute = parseInt(timeArray[1]);
            eventSlot.periodicity = 5;

            var maxDuration = ((30 * 24) - 1) * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
    }

    $("#explanation").html(getExplanationForSchedule(eventSlot));

    return eventSlot;
}

function getExplanationForSchedule(scheduleObject) {
    let zonesString = scheduleObject.zones.join(', ');
    let durationStr = moment.duration(scheduleObject.duration, 'minutes').format('HH[h]:mm[m]');

    var getExampleText = function (currDate, scheduleObject) {
        var endDate = moment(currDate);
        var format = 'YYYY-MM-DD HH:mm:ss';
        endDate.add(scheduleObject.duration, 'm');
        return `<p>Ex: ${currDate.format(format)} - ${endDate.format(format)}</p>`;
    }

    switch (scheduleObject.periodicity) {
        case 0:
            var currDate = moment();
            currDate.minute(scheduleObject.minute).second(scheduleObject.second);
            explanationString = `Irrigation for zone(s) ${zonesString} every hour on ${currDate.format('mm[m]:ss[s]')} with a duration of ${durationStr}\n`;
            for (var i = 0; i < 3; i++) {
                currDate.add(1, 'h');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 1:
            var currDate = moment();
            currDate.minute(scheduleObject.minute).second(scheduleObject.second);
            explanationString = `Irrigation for zone(s) ${zonesString} every ${scheduleObject.hours} hours on ${currDate.format('mm[m]:ss[s]')} with a duration of ${durationStr}\n`;
            for (var i = 0; i < 3; i++) {
                currDate.add(scheduleObject.hours, 'h');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 2:
            var currDate = moment();
            currDate.hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            explanationString = `Irrigation for zone(s) ${zonesString} every day on ${currDate.format('hh[h]:mm[m][00s]')} with a duration of ${durationStr}\n`;
            for (var i = 0; i < 3; i++) {
                currDate.add(1, 'd');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 3:
            var currDate = moment();
            currDate.hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            explanationString = `Irrigation for zone(s) ${zonesString} every ${scheduleObject.days} days on ${currDate.format('hh[h]:mm[m][00s]')} with a duration of ${durationStr}\n`;
            for (var i = 0; i < 3; i++) {
                currDate.add(scheduleObject.days, 'd');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 4:
            var currDate = moment();
            var dayOfWeekStr = weekNames[scheduleObject.dayOfWeek - 1];
            currDate.day(scheduleObject.dayOfWeek - 1).hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            explanationString = `Irrigation for zone(s) ${zonesString} every ${dayOfWeekStr} on ${currDate.format('hh[h]:mm[m][00s]')} with a duration of ${durationStr}\n`;
            for (var i = 0; i < 3; i++) {
                currDate.add(1, 'w');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 5:
            var currDate = moment();
            currDate.hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            currDate.date(scheduleObject.dayOfMonth);
            explanationString = `Irrigation for zone(s) ${zonesString} every month on ${currDate.format('hh[h]:mm[m][00s]')} with a duration of ${durationStr}\n`;
            for (var i = 0; i < 3; i++) {
                currDate.add(1, 'M');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
    }

    return explanationString;
}

function addSchedule() {
    if ($('#schedule-mode-zones .btn-default.active').length === 0) {
        alert("Select minimum one zone!");
        $('#schedule-mode-zones input[type="checkbox"]:eq(0)').click();
        return;
    }

    var eventSlot = getSchedule();

    if (confirm("Are you sure?")) {
        var command = {};
        command.command = "addSchedule";
        command.data = eventSlot;
        console.log(command, JSON.stringify(command));
        //$('.add-schedule').hide();
        ws.send(JSON.stringify(command));
    }
}

function stopManualIrrigation() {
    ws.send('{"command": "stopManualIrrigation"}');
    $('#manual-mode .stop-irrigation-btn').hide();
    $('#manual-mode .start-irrigation-btn').show();
}

function saveWifiConfig() {
    var command = {};
    command.command = "WiFiConfig";
    command.data = {};
    command.data.ssid = $("#ssid").value;
    command.data.pass = $("#pass").value;

    ws.send(JSON.stringify(command));
}

function getEvents() {
    var command = {};
    command.command = "getEvents";

    ws.send(JSON.stringify(command));
}

function removeEvent(evId) {
    var command = {};
    command.command = "removeEvent";
    command.data = {};
    command.data.evId = evId;

    ws.send(JSON.stringify(command));
}