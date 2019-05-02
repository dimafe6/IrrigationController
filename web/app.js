var websocketServerLocation = "ws://" + location.hostname + "/ws";
var ws;
var weekNames = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thuesday", "Friday", "Saturday"];
var periodicityList = { "-1": "Once", "0": "Hourly", "1": "Every X hours", "2": "Daily", "3": "Every X days", "4": "Weekly", "5": "Monthly" };
var calendarEvents = [];
var availableSlots;
var calendar;
var memChart;

window.addEventListener('beforeunload', (event) => {
    ws.close();
});

$(document).ready(function () {
    setInterval(function () {
        getSysInfo();
    }, 5000);

    moment.updateLocale('en', {
        week: {
            dow: 0,
            doy: 6
        }
    });

    WebSocketBegin(websocketServerLocation);

    $('a[href="#calendar-page"]').click(function () {
        setTimeout(function () {
            $('#calendar').fullCalendar('refetchEvents');
            $('#calendar').fullCalendar('rerenderEvents');
        },
            100);
    });

    calendar = $('#calendar').fullCalendar({
        slotDuration: "00:15:00",
        defaultView: 'agendaWeek',
        displayEventTime: true,
        displayEventEnd: true,
        allDaySlot: false,
        height: "auto",
        validRange: {
            start: moment().format("YYYY-MM-DD")
        },
        events: function (start, end, timezone, callback) {
            var events = [];

            var processInterval = function (interval, slot) {
                var occurences = interval.all();
                for (var i = 0; i < occurences.length; i++) {
                    occurences[i].minute(slot.minute).hour(slot.hour).second(slot.second || 0);
                    var startDate = moment(occurences[i]);
                    if (startDate < moment()) {
                        continue;
                    }
                    var endDate = moment(startDate);
                    endDate.add(slot.duration, 'm');
                    events.push({
                        title: slot.title,
                        start: startDate,
                        end: endDate
                    });
                };
            };

            if (calendarEvents.slots !== undefined) {
                $.each(calendarEvents.slots, function (index, slot) {
                    if (slot.enabled) {
                        var currDate = moment(start);
                        if (start < moment()) {
                            currDate = moment();
                        }

                        switch (slot.periodicity) {
                            case 0:
                                currDate.minute(slot.minute).second(slot.second);
                                if (currDate < moment()) {
                                    currDate.add(1, 'h');
                                }
                                while (currDate >= start && currDate < end) {
                                    var endDate = moment(currDate);
                                    endDate.add(slot.duration, 'm');
                                    var startDate = moment(currDate);
                                    events.push({
                                        title: slot.title,
                                        start: startDate,
                                        end: endDate
                                    });

                                    currDate.add(1, 'h');
                                }
                                break;
                            case 1:
                                currDate.minute(slot.minute).second(slot.second);
                                if (currDate < moment()) {
                                    currDate.add(slot.hours, 'h');
                                }
                                while (currDate >= start && currDate < end) {
                                    var endDate = moment(currDate);
                                    endDate.add(slot.duration, 'm');
                                    var startDate = moment(currDate);
                                    events.push({
                                        title: slot.title,
                                        start: startDate,
                                        end: endDate
                                    });

                                    currDate.add(slot.hours, 'h');
                                }
                                break;
                            case 2:
                                var interval = moment(currDate).recur(start, end).every(1).day();
                                processInterval(interval, slot);
                                break;
                            case 3:
                                var interval = moment(currDate).recur(start, end).every(slot.days).day();
                                processInterval(interval, slot);
                                break;
                            case 4:
                                var interval = moment(currDate).recur(start, end).every(weekNames[slot.dayOfWeek]).dayOfWeek();
                                processInterval(interval, slot);
                                break;
                            case 5:
                                var interval = moment(currDate).recur(start, end).every(slot.dayOfMonth).dayOfMonth();
                                processInterval(interval, slot);
                                break;
                        }
                    }
                });

                callback(events);
            }
        }
    })

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

    $('a[data-toggle="tab"]').click(function () {
        if ($('.navbar-collapse.collapse.in').length) {
            $('button.navbar-toggle').click();
        }
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
                $periodBlock.find('.time-hours').pickatime().pickatime('picker').set('select', [slot.hours, 0]);
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

    memChart = new Chart.Line($('#mem-chart'), {
        data: {
            labels: [],
            datasets: [
                {
                    label: "Total",
                    data: [],
                    borderWidth: 1,
                    backgroundColor: [
                        'rgba(0, 255, 26, 0.4)',
                    ],
                },
                {
                    label: "Free",
                    data: [],
                    borderWidth: 1,
                    backgroundColor: [
                        'rgba(38, 0, 255, 0.4)',
                    ],
                },
                {
                    label: "Min allocated",
                    data: [],
                    borderWidth: 1,
                    backgroundColor: [
                        'rgba(255, 0, 0, 0.4)',
                    ],
                },
                {
                    label: "Max allocated",
                    data: [],
                    borderWidth: 1,
                    backgroundColor: [
                        'rgba(255, 174, 0, 0.4)',
                    ],
                }
            ]
        },
        options: {
            title: {
                display: true,
                text: "Memory chart"
            },
            tooltips: {
                mode: 'index',
                intersect: false,
                callbacks: {
                    label: function (t, d) {
                        var label = d.datasets[t.datasetIndex].label || '';
                        return `${label}: ${t.yLabel.toString()}KB`;
                    }
                }
            },
        }
    });
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
    $('#schedule-mode .duration').val(5).trigger('change');
    var $periodBlock = $('.period-block.hourly');
    $periodBlock.find('.time-minute').pickatime().pickatime('picker').set('select', 0);
    $periodBlock.find('.time-second').pickatime().pickatime('picker').set('select', 0);
}

function notify(text, type) {
    $.notify(text, { type: type, placement: { from: 'top', align: 'right', delay: 2000 } });
}

function getSlotById(evId) {
    return calendarEvents.slots[evId] || null;
};

function processSlots(data = null) {
    calendarEvents = data;    
    var $eventTableBody = $('#events-list tbody');
    if (null !== calendarEvents) {
        $('#calendar').fullCalendar('refetchEvents');
        $('#calendar').fullCalendar('rerenderEvents');

        var slots = calendarEvents.slots;
        $eventTableBody.empty();
        var total = parseInt(calendarEvents.total);
        var occupied = parseInt(calendarEvents.occupied);
        var available = total - occupied;
        var statisticText = `<b>${occupied}</b> slots out of <b>${total}</b> are occupied. <b>${available}</b> slots available for adding`;
        $('#events-statistic').html(statisticText);

        availableSlots = available;

        $('#schedule-mode .add-schedule').prop('disabled', availableSlots <= 0);

        for (var i = 0; i < total; i++) {
            var slot = getSlotById(i);
            var enabled = null !== slot ? slot.enabled : true;
            enabled = isNaN(enabled) ? true : enabled;
            var tr = '<tr><td colspan="5">Free slot</td></tr>';
            var disableBtn = `<li role="presentation"><a class="action-disable" role="menuitem"><i class="fa fa-power-off"></i> Disable</a></li>`;
            var enableBtn = `<li role="presentation"><a class="action-enable" role="menuitem"><i class="fa fa-play text-success"></i> Enable</a></li>`;
            var enableDisableBtn = enabled ? disableBtn : enableBtn;

            var actions = `<td class="event-actions">
                <div class="dropdown">
                <button class="btn btn-xs btn-default dropdown-toggle action-btn" type="button" data-toggle="dropdown">Actions <span class="caret"></span></button>
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
                var duration = moment.duration(slot.duration, 'minutes').format('HH[h]:mm[m]');
                var periodicity = periodicityList[slot.periodicity] || null;
                var zones = slot.zones ? JSON.stringify(slot.zones) : '';
                var color = enabled ? "#333" : "#999";

                tr = `<tr data-enabled="${enabled}" style="color: ${color}">
                    <td class="evId" data-evid="${i}">${i}</td>
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
            $('#ws-n-conn').hide();
            getSlots();
            getSysInfo();
        };

        ws.onmessage = function (evt) {
            var jsonObject = JSON.parse(evt.data);

            console.log(jsonObject);
            var command = jsonObject.command || null;
            if (null !== command) {
                var data = jsonObject.data || null;
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
                    case 'getSlots':
                        processSlots(data);
                        $('.add-schedule').prop("disabled", !(availableSlots > 0));
                        break;
                    case 'getSysInfo':
                        if (data['WiFi']) {
                            $('#wifi-ssid').text(data['WiFi']['SSID']);
                            $('#wifi-ip').text(data['WiFi']['localIP']);
                            $('#wifi-rssi').text(data['WiFi']['RSSI']);
                        }

                        if (data['SD']) {
                            $('#sd-total').text(data['SD']['total']);
                            $('#sd-used').text(data['SD']['used']);
                            $('#sd-type').text(data['SD']['type']);
                        }

                        if (data['SPIFFS']) {
                            $('#spiffs-total').text(data['SPIFFS']['total']);
                            $('#spiffs-used').text(data['SPIFFS']['used']);
                        }

                        if (data['heap']) {
                            var total = Math.floor(parseInt(data['heap']['total']) / 1024);
                            var free = Math.floor(parseInt(data['heap']['free']) / 1024);
                            var min = Math.floor(parseInt(data['heap']['min']) / 1024);
                            var max = Math.floor(parseInt(data['heap']['maxAlloc']) / 1024);

                            $('#mem-total').text(total);
                            $('#mem-free').text(free);
                            $('#mem-min').text(min);
                            $('#mem-max').text(max);

                            memChart.data.labels.push(moment().format("HH:mm:ss"));
                            memChart.data.datasets[0].data.push(total);
                            memChart.data.datasets[1].data.push(free);
                            memChart.data.datasets[2].data.push(min);
                            memChart.data.datasets[3].data.push(max);
                            memChart.update();
                        }
                        break;
                    case 'ongoingEvents':
                        $('.zone-panel').removeClass('active');
                        $('.next-start-date, .next-finish-date, .next-duration, .start-date, .finish-date, .elapsed-time').html("N/A");
                        $.each(data, function (index, occurence) {
                            $.each(occurence.zones, function (index, zone) {
                                var zoneId = index + 1;
                                var $zonePanelBody = $('div[data-zone="' + zoneId + '"]');
                                var $zonePanel = $zonePanelBody.closest('.zone-panel');
                                if (zone && !$zonePanel.hasClass('active')) {
                                    var startDate = moment.unix(occurence.from);
                                    var finishDate = moment.unix(occurence.to);
                                    var elapsed = moment.duration(finishDate - moment(), "milliseconds").format("D[d] H[h] m[m] s[s]");
                                    $zonePanel.find('.start-date').html(startDate.format('YYYY-MM-DD HH:mm:ss'));
                                    $zonePanel.find('.finish-date').html(finishDate.format('YYYY-MM-DD HH:mm:ss'));
                                    $zonePanel.find('.elapsed-time').html(elapsed);
                                    $zonePanel.addClass('active');
                                }
                            });
                        });
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
    eventSlot.title = $('.event-title').val();
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
    let title = "";

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
            title = "Every hour";

            for (var i = 0; i < 3; i++) {
                currDate.add(1, 'h');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 1:
            var currDate = moment();
            currDate.minute(scheduleObject.minute).second(scheduleObject.second);
            explanationString = `Irrigation for zone(s) ${zonesString} every ${scheduleObject.hours} hours on ${currDate.format('mm[m]:ss[s]')} with a duration of ${durationStr}\n`;
            title = `Every ${scheduleObject.hours} hours`;

            for (var i = 0; i < 3; i++) {
                currDate.add(scheduleObject.hours, 'h');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 2:
            var currDate = moment();
            currDate.hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            explanationString = `Irrigation for zone(s) ${zonesString} every day on ${currDate.format('hh[h]:mm[m][00s]')} with a duration of ${durationStr}\n`;
            title = `Every day`;

            for (var i = 0; i < 3; i++) {
                currDate.add(1, 'd');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
        case 3:
            var currDate = moment();
            currDate.hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            explanationString = `Irrigation for zone(s) ${zonesString} every ${scheduleObject.days} days on ${currDate.format('hh[h]:mm[m][00s]')} with a duration of ${durationStr}\n`;
            title = `Every ${scheduleObject.days} days`;

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
            title = `Every ${dayOfWeekStr}`;

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
            title = `Every month`;

            for (var i = 0; i < 3; i++) {
                currDate.add(1, 'M');
                explanationString += getExampleText(currDate, scheduleObject);
            }
            break;
    }

    var total = parseInt(calendarEvents.occupied) || 0;
    title = `${total}. ${title}`;

    $('.event-title').val(title);

    return explanationString;
}

function addOrEditSchedule() {
    var eventSlot = getSchedule();

    if (availableSlots <= 0 && isNaN(eventSlot.evId)) {
        alert("There are no available slots");
        return;
    }

    if ($('#schedule-mode-zones .btn-default.active').length === 0) {
        alert("Select minimum one zone!");
        $('#schedule-mode-zones input[type="checkbox"]:eq(0)').click();
        return;
    }

    if (confirm("Are you sure?")) {
        var command = {};
        command.command = "addOrEditSchedule";
        command.data = eventSlot;
        console.log(command, JSON.stringify(command));
        $('.add-schedule').prop("disabled", 1);
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

function getSlots() {
    var command = {};
    command.command = "getSlots";

    ws.send(JSON.stringify(command));
}

function removeEvent(evId) {
    var command = {};
    command.command = "removeEvent";
    command.data = {};
    command.data.evId = evId;

    ws.send(JSON.stringify(command));
    $('.action-btn').prop('disabled', 1);
}

function setEventEnabled(evId, enabled = true) {
    var command = {};
    command.command = "setEventEnabled";
    command.data = {};
    command.data.evId = evId;
    command.data.enabled = enabled;

    ws.send(JSON.stringify(command));
    $('.action-btn').prop('disabled', 1);
}

function getSysInfo() {
    var command = {};
    command.command = "getSysInfo";

    ws.send(JSON.stringify(command));
}