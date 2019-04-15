var websocketServerLocation = "ws://" + location.hostname + "/ws";
var ws;
var weekNames = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thuesday", "Friday", "Saturday"];
var periodicityList = { "-1": "Once", "0": "Hourly", "1": "Every X hours", "2": "Daily", "3": "Every X days", "4": "Weekly", "5": "Monthly" };
var calendarEvents = [];

window.addEventListener('beforeunload', (event) => {
    ws.close();
});

$(document).ready(function () {
    processGetEvents(); //TODO: only for test

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
        onSet: getSchedule
    });

    $('.time-hour-minute').pickatime({
        format: 'HH:i',
        interval: 15,
        clear: '',
        onSet: getSchedule
    }).pickatime('picker').set('select', 0);

    $('.time-hours').pickatime({
        format: 'HH',
        interval: 60,
        min: [1, 0],
        max: [23, 0],
        clear: '',
        onSet: getSchedule
    }).pickatime('picker').set('select', 1);

    $('.time-day-of-month').pickatime({
        format: 'i',
        interval: 1,
        min: [0, 1],
        max: [0, 28], //Minimum days for each months
        clear: '',
        onSet: getSchedule
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

    $(document).on('click', '.event-actions .action-remove', function () {
        var evId = parseInt($(this).closest('tr').find('td:first').text());
        removeEvent(evId);
    });

    $(document).on('click', '.event-actions .action-edit', function () {
        var evId = parseInt($(this).closest('tr').find('td:first').text());
        var slot = getSlotById(evId);
        if (null == slot) {
            return;
        }

        cancelEditSchedule();

        $('#evId').val(evId);
        $('html, body').animate({
            scrollTop: $("#add-event-header").offset().top
        }, 500);
        $('#events-list tbody td.evId[data-evid="' + evId + '"]').closest('tr').addClass('bg-warning');
        $('#add-event-header').text('Edit event #' + evId);
        $('#schedule-mode .add-schedule').hide();
        $('#schedule-mode .save-schedule').show();
        $('#schedule-mode .cancel-edit-schedule').show();

        $('#schedule-mode-zones label').removeClass('active');
        $.each(slot.zones, function (index, value) {
            $('#schedule-mode-zones input[value="' + value + '"]').parent().addClass('active');
        });

        $('#periodicity').val(slot.periodicity).trigger('change');
        $('#schedule-mode .duration').val(slot.duration).trigger('change');

        switch (slot.periodicity) {
            case 0:
                var $periodBlock = $('.period-block.hourly');
                $periodBlock.find('.time-minute').pickatime().pickatime('picker').set('select', slot.minute);
                $periodBlock.find('.time-second').pickatime().pickatime('picker').set('select', slot.second);
                break;
            case 1:
                var $periodBlock = $('.period-block.every-x-hours');
                $periodBlock.find('.time-hours').pickatime().pickatime('picker').set('select', slot.hours);
                $periodBlock.find('.time-minute').pickatime().pickatime('picker').set('select', slot.minute);
                $periodBlock.find('.time-second').pickatime().pickatime('picker').set('select', slot.second);
                break;
            case 2:
                var $periodBlock = $('.period-block.daily');
                $periodBlock.find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);
                break;
            case 3:
                var $periodBlock = $('.period-block.every-x-days');
                $periodBlock.find('.time-days').val(slot.days).trigger('change');
                $periodBlock.find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);
                break;
            case 4:
                var $periodBlock = $('.period-block.weekly');
                $periodBlock.find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);

                $('#weekdays-selector label').removeClass('active');
                $('#weekdays-selector input[value="' + slot.dayOfWeek + '"]').parent().addClass('active');
                break;
            case 5:
                var $periodBlock = $('.period-block.monthly');
                $periodBlock.find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);
                $periodBlock.find('.time-day-of-month').pickatime().pickatime('picker').set('select', slot.dayOfMonth);
                break;
        }
    });

    $(document).on('click', '.event-actions .action-disable', function () {
        var evId = parseInt($(this).closest('tr').find('td:first').text());
        setEventEnabled(evId, false);
    });

    $(document).on('click', '.event-actions .action-enable', function () {
        var evId = parseInt($(this).closest('tr').find('td:first').text());
        setEventEnabled(evId, true);
    });

    $(document).on('click', '.cancel-edit-schedule', cancelEditSchedule);
});

function cancelEditSchedule() {
    $('#evId').val('');
    $('#events-list tbody tr').removeClass('bg-warning');
    $('#add-event-header').text('Add event');
    $('#schedule-mode .add-schedule').show();
    $('#schedule-mode .save-schedule').hide();
    $('#schedule-mode .cancel-edit-schedule').hide();
    $('#schedule-mode-zones label').removeClass('active');
    $('#schedule-mode-zones input[value="1"]').parent().addClass('active');
    $('#periodicity').val(0).trigger('change');
    $('#duration').val(5).trigger('change');
    var $periodBlock = $('.period-block.hourly');
    $periodBlock.find('.time-minute').pickatime().pickatime('picker').set('select', 0);
    $periodBlock.find('.time-second').pickatime().pickatime('picker').set('select', 0);
}

function notify(text, type) {
    $.notify(text, { type: type, placement: { from: 'top', align: 'center' } });
}

function getSlotById(evId) {
    var slot = null;
    $.each(calendarEvents.slots, function (index, val) {
        if (val.evId === evId) {
            slot = val;
        }
    });

    return slot;
};

function processGetEvents(data = null) {
    //TODO: Only for test
    var jsonObject = JSON.parse('{"command":"getEvents","data":{"total":25,"occupied":6,"slots":[{"evId":0,"duration":5,"zones":[1,2,4],"minute":10,"second":15,"periodicity":0},{"evId":1,"duration":60,"zones":[1,2],"hours":10,"minute":5,"second":15,"periodicity":1},{"evId":2,"duration":30,"zones":[1,4],"hour":2,"minute":30,"periodicity":2},{"evId":3,"duration":45,"zones":[1,3],"days":5,"hour":1,"minute":30,"periodicity":3},{"evId":4,"duration":10,"zones":[1,3],"dayOfWeek":4,"hour":0,"minute":15,"periodicity":4},{"evId":5,"duration":135,"zones":[1,3],"dayOfMonth":5,"hour":3,"minute":15,"periodicity":5}]}}');
    console.log(jsonObject);
    var command = jsonObject.command || null;
    var data = jsonObject.data || null;
    //TODO: Only for test
    calendarEvents = data;
    var $eventTableBody = $('#events-list tbody');
    if (null !== calendarEvents) {
        var slots = calendarEvents.slots;
        $eventTableBody.empty();
        var total = parseInt(calendarEvents.total);
        var occupied = parseInt(calendarEvents.occupied);
        var available = total - occupied;
        var statisticText = `<b>${occupied}</b> slots out of <b>${total}</b> are occupied. <b>${available}</b> slots available for adding`;
        $('#events-statistic').html(statisticText);

        for (var i = 0; i < total; i++) {
            var slot = getSlotById(i);
            var enabled = parseInt(null !== slot ? slot.enabled : true);
            enabled = isNaN(enabled) ? true : enabled;
            var tr = '<tr><td colspan="5">Free slot</td></tr>';
            var disableBtn = `<li role="presentation"><a class="action-disable" role="menuitem"><i class="fa fa-power-off"></i> Disable</a></li>`;
            var enableBtn = `<li role="presentation"><a class="action-enable" role="menuitem"><i class="fa fa-play text-success"></i> Enable</a></li>`;
            var enableDisableBtn = enabled ? disableBtn : enableBtn;

            var actions = `<td class="event-actions">
                <div class="dropdown">
                <button class="btn btn-xs btn-default dropdown-toggle" type="button" data-toggle="dropdown">Actions <span class="caret"></span></button>
                <ul class="dropdown-menu dropdown-menu-right" role="menu">
                <li role="presentation">
                <a class="action-edit" role="menuitem"><i class="fa fa-edit text-warning"></i> Edit</a>
                </li>
                <li role="presentation">
                <a class="action-remove" role="menuitem"><i class="fa fa-trash text-danger"></i> Remove</a>
                </li>
                ${enableDisableBtn}
                </ul>
                </div>
                </td>`;

            if (null !== slot) {
                var evId = slot.evId;
                var duration = moment.duration(slot.duration, 'minutes').format('HH[h]:mm[m]');
                var periodicity = periodicityList[slot.periodicity] || null;
                var zones = slot.zones ? JSON.stringify(slot.zones) : '';
                var color = enabled ? "#333" : "#999";

                tr = `<tr data-enabled="${enabled}" style="color: ${color}">
                    <td class="evId" data-evid="${evId}">${evId}</td>
                    <td>${periodicity}</td>
                    <td>${duration}</td>
                    <td>${zones}</td>
                    ${actions}
                    </tr>`;
            }

            $eventTableBody.append(tr);
        }
    }
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
                    case 'addOrEditSchedule':
                        notify('Schedule added/updated', 'success');
                        cancelEditSchedule();
                        break;
                    case 'getEvents':
                        processGetEvents(data);
                        break;
                }
            }
        };

        ws.onclose = function () {
            $('#ws-n-conn').show();
            setTimeout(function () { WebSocketBegin(websocketServerLocation) }, 5000);
        };

        ws.onerror = function (error) {
            if (error.message !== undefined) {
                notify('WS error: ' + error.message, 'danger');
            }
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
    var checked = [];
    var evId = parseInt($('#evId').val());

    $('#schedule-mode-zones input[type="checkbox"]').each(function () {
        if ($(this).parent().hasClass('active')) {
            checked.push(parseInt($(this).val()));
        }
    });

    eventSlot.duration = parseInt($scheduleBlock.find('.duration').val());
    eventSlot.zones = checked;

    // If edit event
    if (!isNaN(evId)) {
        eventSlot.evId = evId;
    }

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

function addOrEditSchedule() {
    if ($('#schedule-mode-zones .btn-default.active').length === 0) {
        alert("Select minimum one zone!");
        $('#schedule-mode-zones input[type="checkbox"]:eq(0)').click();
        return;
    }

    var eventSlot = getSchedule();

    if (confirm("Are you sure?")) {
        var command = {};
        command.command = "addOrEditSchedule";
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

function setEventEnabled(evId, enabled = true) {
    console.log(enabled);
    var command = {};
    command.command = "setEventEnabled";
    command.data = {};
    command.data.evId = evId;
    command.data.enabled = enabled;

    ws.send(JSON.stringify(command));
}