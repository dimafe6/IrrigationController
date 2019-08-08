const weekNames = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thuesday", "Friday", "Saturday"];
const accuWeatherAPIKey = "dDDoPqyCFBSiBC2NeODxjvyrruUO4mgu";
const accuWeatherApiDomain = 'http://dataservice.accuweather.com';
const apixuAPIKey = "4e3720d2b7234ec8b8585710191907";
const apixuApiDomain = 'http://api.apixu.com/v1';
const websocketServerLocation = `ws://${location.hostname}/ws`;
const manualIrrigationEventId = 25;

let ws;
let calendarEvents = {};
let availableSlots;
let calendar;
let currentTime = null;
let eventsTable = null;
let channelNames = [];

let compareOccurences = (a, b) => (a.from < b.from ? 1 : -1);

window.addEventListener('beforeunload', (event) => ws.close());

$(document).ready(() => {
    $('#datetimepicker').datetimepicker({
        inline: true,
        sideBySide: true,
        format: 'YYYY-MM-DD HH:mm:ss'
    });

    $('#one-time-datetimepicker').datetimepicker({
        inline: true,
        sideBySide: true,
        format: 'YYYY-MM-DD HH:mm:ss',
        minDate: new Date()
    });

    $(document).on('click', '.save-time-btn', () => setTime($('#datetimepicker').data("DateTimePicker").viewDate()));

    setInterval(() => {
        if (currentTime) {
            currentTime.add(1, 'seconds');
            $('#current-time').text(currentTime.format('ddd, YYYY-MM-DD HH:mm:ss'));
        }
        $('#one-time-datetimepicker').data("DateTimePicker").minDate(new Date());
    }, 1000);

    moment.updateLocale('en', {
        week: {
            dow: 0,
            doy: 6
        }
    });

    WebSocketBegin(websocketServerLocation);

    $('a[href="#calendar-page"]').click(() => {
        setTimeout(() => {
            $('#calendar').fullCalendar('refetchEvents');
            $('#calendar').fullCalendar('rerenderEvents');
        },
            100);
    });

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
        min: [2, 0],
        max: [23, 0],
        clear: '',
        onSet: getSchedule
    }).pickatime('picker').set('select', 2);

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

    $('a[data-toggle="tab"]').click(() => {
        if ($('.navbar-collapse.collapse.in').length) {
            $('button.navbar-toggle').click();
        }
    });

    $('#periodicity')
        .change(function () {
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
                case 6:
                    $('.period-block ').hide();
                    $('.period-block.one-time').show();
                    break;
                default:
                    $('.period-block ').hide();
                    break;
            }

            getSchedule();
        })
        .change();

    $('#schedule-mode-zones,.time-days,#weekdays-selector input,#schedule-mode .duration').change(getSchedule);

    $(document).on('click', '.event-action-remove', function () {
        removeEvent(+$(this).closest('tr').find('td:first').text());
    });

    $(document).on('click', '.event-action-edit', function () {
        let evId = parseInt($(this).closest('tr').find('td:first').text());
        let slot = getSlotById(evId);
        if (!slot) {
            return;
        }

        cancelEditSchedule();

        $('#evId').val(evId);
        $('html, body').animate({
            scrollTop: $("#add-event-header").offset().top
        }, 500);
        $(`#events-list tbody td.evId[data-evid="${evId}"]`).closest('tr').addClass('bg-warning');
        $('#add-event-header').text(`Edit event #${evId}`);
        $('#schedule-mode .add-schedule').hide();
        $('#schedule-mode .save-schedule').show();
        $('#schedule-mode .cancel-edit-schedule').show();
        $('#schedule-mode .event-title').val(slot.title);
        $('#schedule-mode-zones option:selected').removeAttr('selected');

        for (let value of slot.channels) {
            $(`#schedule-mode-zones option:eq(${value})`).attr('selected', true);
        }

        $('#periodicity').val(slot.periodicity).trigger('change');
        $('#schedule-mode .duration').val(slot.duration).trigger('change');

        switch (slot.periodicity) {
            case 0:
                $('.period-block.hourly').find('.time-minute').pickatime().pickatime('picker').set('select', slot.minute);
                $('.period-block.hourly').find('.time-second').pickatime().pickatime('picker').set('select', slot.second);
                break;
            case 1:
                $('.period-block.every-x-hours').find('.time-hours').pickatime().pickatime('picker').set('select', [slot.hours, 0]);
                $('.period-block.every-x-hours').find('.time-minute').pickatime().pickatime('picker').set('select', slot.minute);
                $('.period-block.every-x-hours').find('.time-second').pickatime().pickatime('picker').set('select', slot.second);
                break;
            case 2:
                $('.period-block.daily').find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);
                break;
            case 3:
                $('.period-block.every-x-days').find('.time-days').val(slot.days).trigger('change');
                $('.period-block.every-x-days').find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);
                break;
            case 4:
                $('.period-block.weekly').find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);
                $('#weekdays-selector label').removeClass('active');
                $(`#weekdays-selector input[value="${slot.dayOfWeek}"]`).parent().addClass('active');
                break;
            case 5:
                $('.period-block.monthly').find('.time-hour-minute').pickatime().pickatime('picker').set('select', [slot.hour, slot.minute]);
                $('.period-block.monthly').find('.time-day-of-month').pickatime().pickatime('picker').set('select', slot.dayOfMonth);
                break;
            case 6:
                $('#one-time-datetimepicker')
                    .data("DateTimePicker")
                    .date(moment([slot.year, slot.month - 1, slot.day, slot.hour, slot.minute, slot.second, 0]));
                break;
        }
    });

    $(document).on('click', '.event-action-disable', function () {
        setEventEnabled(+$(this).closest('tr').find('td:first').text(), false);
    });

    $(document).on('click', '.event-action-enable', function () {
        setEventEnabled(+$(this).closest('tr').find('td:first').text(), true);
    });

    $(document).on('click', '.cancel-edit-schedule', cancelEditSchedule);

    $(document).on('click', '.running-info .skip-btn', function () {
        let evId = +$(this).closest('.running-info').attr('data-evid');
        if (evId === manualIrrigationEventId) {
            if (confirm("Are you sure?")) {
                stopManualIrrigation();
            }
        } else {
            skipEvent(evId);
        }
    });

    $(document).on('click', 'a[href="#schedule-mode"]', getSchedule);

    $(document).on('click', '.save-channel-names-btn', function () {
        $(this).prop('disabled', true);
        var channelNames = [];
        $('#channel-names tbody tr').map(function () {
            var id = +$(this).find('td:eq(0)').html();
            var name = $(this).find('td:eq(1) input').val();
            if (name.trim().length <= 0) {
                notify(`Wrong name "${name}" for channel ${id}`, "danger");
                $(this).find('tr:eq(1) input').focus();
            } else {
                channelNames.push({ id, name });
            }
        });

        sendWSCommand("saveChannelNames", channelNames);
    });

    initCalendar();

    $('.location-typeahead').typeahead({
        display: 'name',
        delay: 1000,
        source: (query, process) => $.get(`${apixuApiDomain}/search.json?key=${apixuAPIKey}`, { q: query }, (data) => process(data)),
        afterSelect: (item) => {
            let settings = updateObjectInLocalStorage("settings", { lat: item.lat, lon: item.lon, location: item.name });
            $.get(`${accuWeatherApiDomain}/locations/v1/cities/geoposition/search?apikey=${accuWeatherAPIKey}&q=${settings.lat},${settings.lon}&toplevel=true`, (locationData) => {
                settings = updateObjectInLocalStorage("settings", {
                    elevation: locationData.GeoPosition.Elevation.Metric.Value,
                    accuWeatherCityKey: locationData.Key
                });

                sendWSCommand("saveSettings", settings);

                fetchWeatherForecast();
            });
        }
    });

    $(document).on('click', '.forecast-apixu', showForecastFromApixuOnCalendar);

    $(document).on('click', '.forecast-accuweather', showForecastFromAccuWeatherOnCalendar);
});

function sendWSCommand(command, data = null) {
    console.log(JSON.stringify(!data ? { command } : { command, data }));
    ws.send(JSON.stringify(!data ? { command } : { command, data }));
}

function initCalendar() {
    calendar = $('#calendar').fullCalendar({
        themeSystem: 'bootstrap3',
        slotDuration: "00:15:00",
        defaultView: 'month',
        header: {
            left: 'prev,next,today',
            center: 'title',
            right: 'month,agendaWeek,listWeek,agendaDay'
        },
        displayEventTime: true,
        displayEventEnd: true,
        allDaySlot: false,
        height: "auto",
        validRange: {
            start: moment().format("YYYY-MM-DD")
        },
        selectable: true,
        selectHelper: true,
        nowIndicator: true,
        eventLimitText: 'events',
        eventBackgroundColor: '#3a87ad59',
        eventBorderColor: '#3a87ad59',
        eventTextColor: '#000000d6',
        eventLimit: 1,
        selectAllow: (info) => {
            if (calendar.fullCalendar('getView').name === 'month')
                return false;
            if (info.start.isBefore(moment()))
                return false;
            return true;
        },
        select: (startDate, endDate) => {
            $('#periodicity').val(6).trigger('change');
            $('#one-time-datetimepicker').data("DateTimePicker").date(startDate);
            $('#schedule-mode .duration').val(moment.duration(endDate.diff(startDate)).as('minutes'));
            $("a[href='#schedule-mode']").click();
        },
        events: (start, end, timezone, callback) => {
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
                $.each(calendarEvents.slots, (index, slot) => {
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
                            case 6:
                                var startDate = moment([slot.year, slot.month - 1, slot.day, slot.hour, slot.minute, slot.second, 0]);
                                var endDate = moment(startDate);
                                endDate.add(slot.duration, 'm');
                                events.push({
                                    title: slot.title,
                                    start: startDate,
                                    end: endDate
                                });
                                break;
                        }
                    }
                });

                callback(events);
            }
        },
        viewRender: fetchWeatherForecast
    });
}

function getMomentFromEpoch(epoch) {
    return moment(moment.unix(epoch).utc().format("YYYY-MM-DD HH:mm:ss"));
}

function cancelEditSchedule() {
    $('#evId').val('');
    $('#events-list tbody tr').removeClass('bg-warning');
    $('#add-event-header').text('Add event');
    $('#schedule-mode .add-schedule').show();
    $('#schedule-mode .save-schedule').hide();
    $('#schedule-mode .cancel-edit-schedule').hide();
    $('#schedule-mode-zones option:selected').removeAttr('selected');
    $('#schedule-mode-zones option:first').attr('selected', true);
    $('#periodicity').val(0).trigger('change');
    $('#schedule-mode .duration').val(5).trigger('change');
    $('.period-block.hourly').find('.time-minute').pickatime().pickatime('picker').set('select', 0);
    $('.period-block.hourly').find('.time-second').pickatime().pickatime('picker').set('select', 0);
}

function notify(text, type) {
    $.notify(text, { type: type, placement: { from: 'top', align: 'right', delay: 2000 } });
}

function getSlotById(evId) {
    return calendarEvents.slots[evId] || null;
};

function processSlots(data = null) {
    let $eventTableBody = $('#events-list tbody');
    calendarEvents = data;
    if (calendarEvents) {
        $eventTableBody.empty();
        $('#calendar').fullCalendar('refetchEvents');
        $('#calendar').fullCalendar('rerenderEvents');

        let total = +calendarEvents.total;
        let occupied = +calendarEvents.occupied;
        let available = total - occupied;
        let statisticText = `<b>${occupied}</b> slots out of <b>${total}</b> are occupied. <b>${available}</b> slots available for adding`;
        $('#events-statistic').html(statisticText);

        availableSlots = available;

        $('#schedule-mode .add-schedule').prop('disabled', availableSlots <= 0);

        for (let i = 0; i < total; i++) {
            let slot = getSlotById(i);
            let enabled = slot ? slot.enabled : true;
            enabled = isNaN(enabled) ? true : enabled;
            let tr = '<tr><td colspan="5">Free slot</td></tr>';
            let disableBtn = `<li role="presentation"><a class="event-action-disable" role="menuitem"><i class="fa fa-power-off"></i> Disable</a></li>`;
            let enableBtn = `<li role="presentation"><a class="event-action-enable" role="menuitem"><i class="fa fa-play text-success"></i> Enable</a></li>`;
            let enableDisableBtn = enabled ? disableBtn : enableBtn;

            let actions = `
                    <div class="dropdown">
                        <button class="btn btn-xs btn-default dropdown-toggle action-btn" type="button" data-toggle="dropdown">Actions <span class="caret"></span></button>
                        <ul class="dropdown-menu dropdown-menu-right" role="menu">
                            <li role="presentation">
                                <a class="event-action-edit" role="menuitem"><i class="fa fa-edit text-warning"></i> Edit</a>
                            </li>
                            <li role="presentation">
                                <a class="event-action-remove" role="menuitem"><i class="fa fa-trash text-danger"></i> Remove</a>
                            </li>
                            ${enableDisableBtn}
                        </ul>
                    </div>`;

            if (slot) {
                let duration = moment.duration(slot.duration, 'minutes').format('HH[h]:mm[m]');
                let names = [];
                let color = enabled ? "#333" : "#999";

                $.each(slot.channels, (index, chId) => names.push(channelNames.find(n => n.id === chId).name));

                tr = `<tr data-enabled="${enabled}" style="color: ${color}">
                        <td class="evId" data-evid="${i}">${i}</td>
                        <td>${slot.title}</td>
                        <td>${duration}</td>
                        <td>${names}</td>
                        <td>${actions}</td>
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
            getSettings();
            getWiFiConfig();
            getSysInfo();
            getChannelNames();
        };

        ws.onmessage = function (evt) {
            let jsonObject = JSON.parse(evt.data);
            let command = jsonObject.command || null;
            if (command) {
                let data = jsonObject.data || null;
                switch (command) {
                    case 'manualIrrigation':
                        $('#manual-mode .stop-irrigation-btn').show();
                        $('#manual-mode .start-irrigation-btn').hide();
                        notify('Manual irrigation has been started', 'success');
                        break;
                    case 'stopManualIrrigation':
                        $('#manual-mode .stop-irrigation-btn').hide();
                        $('#manual-mode .start-irrigation-btn').show();
                        notify('Manual irrigation has been finished', 'success');
                        break;
                    case 'addOrEditSchedule':
                        notify('Schedule has been added/updated', 'success');
                        $('.add-schedule').prop("disabled", 0);
                        cancelEditSchedule();
                        break;
                    case 'getSlots':
                        processSlots(data);
                        getSchedule();
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
                            $('#mem-total').text(~~(+data['heap']['total'] / 1024));
                            $('#mem-free').text(~~(+data['heap']['free'] / 1024));
                            $('#mem-min').text(~~(+data['heap']['min'] / 1024));
                            $('#mem-max').text(~~(+data['heap']['maxAlloc'] / 1024));
                        }

                        currentTime = moment(data['time']);
                        $('#current-time').text(currentTime.format('ddd, YYYY-MM-DD HH:mm:ss'));
                        break;
                    case 'ongoingEvents':
                        $('.zone-panel').removeClass('active');
                        $('.start-date, .finish-date, .elapsed-time, .running-info .event-name').html("N/A");
                        data.sort(compareOccurences);
                        $('#manual-mode .stop-irrigation-btn').hide();
                        $('#manual-mode .start-irrigation-btn').show();
                        $.each(data, (index, occurence) => {
                            if (occurence.isManual) {
                                $('#manual-mode .stop-irrigation-btn').show();
                                $('#manual-mode .start-irrigation-btn').hide();
                                $.each(occurence.channels, (index, value) => {
                                    $(`#manual-mode-zones option:eq(${index})`).attr('selected', value == 1);
                                });
                            }
                            $.each(occurence.channels, (index, zone) => {
                                let $zonePanelBody = $('div[data-zone="' + index + '"]');
                                let $zonePanel = $zonePanelBody.closest('.zone-panel');
                                if (zone && !$zonePanel.hasClass('active')) {
                                    let startDate = getMomentFromEpoch(occurence.from);
                                    let finishDate = getMomentFromEpoch(occurence.to);
                                    let elapsed = moment.duration(occurence.elapsed, "seconds").format("D[d] H[h] m[m] s[s]");
                                    $zonePanel.find('.start-date').html(startDate.format('YYYY-MM-DD HH:mm:ss'));
                                    $zonePanel.find('.finish-date').html(finishDate.format('YYYY-MM-DD HH:mm:ss'));
                                    $zonePanel.find('.duration').html(moment.duration((finishDate - startDate), "milliseconds").format("D[d] H[h] m[m] s[s]"));
                                    $zonePanel.find('.elapsed-time').html(elapsed);
                                    if (Object.entries(calendarEvents).length > 0) {
                                        $zonePanel.find('.running-info .event-name').html(occurence.evId === manualIrrigationEventId ? "Manual" : calendarEvents.slots[occurence.evId].title);
                                    }
                                    $zonePanel.addClass('active');
                                    $zonePanelBody.find('.running-info').attr("data-evid", occurence.evId);
                                }
                            });
                        });
                        break;
                    case 'nextEvents':
                        $('.next-start-date, .next-finish-date, .next-elapsed, .next-duration, .next-start .event-name').html("N/A");
                        data.sort(compareOccurences);
                        $.each(data, (index, occurence) => {
                            $.each(occurence.channels, (index, zone) => {
                                let $zonePanelBody = $('div[data-zone="' + index + '"]:not(.active)');
                                let $zonePanel = $zonePanelBody.closest('.zone-panel');
                                if (zone) {
                                    let startDate = getMomentFromEpoch(occurence.from);
                                    let finishDate = getMomentFromEpoch(occurence.to);
                                    let elapsed = moment.duration(occurence.elapsed, "seconds").format("D[d] H[h] m[m] s[s]");
                                    $zonePanel.find('.next-start-date').html(startDate.format('YYYY-MM-DD HH:mm:ss'));
                                    $zonePanel.find('.next-finish-date').html(finishDate.format('YYYY-MM-DD HH:mm:ss'));
                                    $zonePanel.find('.next-duration').html(moment.duration((finishDate - startDate), "milliseconds").format("D[d] H[h] m[m] s[s]"));
                                    $zonePanel.find('.next-elapsed').html(elapsed);
                                    if (Object.entries(calendarEvents).length) {
                                        $zonePanel.find('.next-start .event-name').html(occurence.evId === 25 ? "Manual" : calendarEvents.slots[occurence.evId].title);
                                    }
                                }
                            });
                        });
                        break;
                    case 'getWaterInfo':
                        $('#water-flow').text(data['flow']);
                        $('#water-day').text(data['curDay']);
                        $('#water-month').text(data['curMonth']);
                        break;
                    case 'weatherUpdate':
                        $('.w-temp').html(`${data.temp}°C`);
                        $('.w-press').html(`${data.pressure} hPa`);
                        $('.w-hum').html(`${data.humidity}%`);
                        $('.w-light').html(`${data.light} lux`);
                        $('.w-water-temp').html(`${data.waterTemp}°C`);
                        $('.w-rain').html(`${data.rain}`);
                        $('.w-ground-hum').html(`${data.groundHum}`);
                        break;
                    case 'getChannelNames':
                        channelNames = data;
                        $('#channel-statuses').empty();
                        $('#manual-mode-zones, #schedule-mode-zones').empty();
                        $('#channel-names tbody').empty();

                        let channelsTemplate = $("#dash-channel-status-block div:first").clone();
                        let channelBlock = $("#channel-statuses");
                        $.each(data, function () {
                            let channelStatus = channelsTemplate.clone();
                            channelStatus.find('.channel-id').attr('data-zone', this.id);
                            channelStatus.find('.channel-name').html(this.name);
                            channelStatus.show();
                            $("#channel-statuses").append(channelStatus);
                            $('#manual-mode-zones, #schedule-mode-zones').append(`<option value="${this.id}">${this.name}</option>`);
                            $('#channel-names tbody').append(
                                `<tr>
                                    <td style="vertical-align: middle;">${this.id}</td>
                                    <td style="padding: 0;">
                                        <input style="border: none; padding: 0px 5px;" class="form-control input-sm" maxlength="25" value="${this.name}"></input>
                                    </td>
                                </tr>`
                            );
                        });

                        getSlots();
                        break;
                    case 'saveChannelNames':
                        notify('Channel names have been saved', 'success');
                        $('.save-channel-names-btn').prop('disabled', false);
                        break;
                    case 'debug':
                        $('#logs-textarea').append(`${data}\r\n`);
                        break;
                    case 'getSettings':
                        let settings = updateObjectInLocalStorage("settings", data);
                        $('.location-typeahead').val(settings.location);
                        break;
                    case 'saveSettings':
                        notify('Settings have been saved', 'success');
                        break;
                    case 'getWiFiConfig':
                        $("#ssid").val(data.ssid);
                        $("#pass").val(data.password)
                        break;
                    case 'removeEvent':
                        notify("Event has been removed", 'success');
                        break;
                    case 'enableEvent':
                        notify("Event has been enabled", 'success');
                        break;
                    case 'disableEvent':
                        notify("Event has been disabled", 'success');
                        break;
                    case 'skipEvent':
                        notify("Event has been skipped", 'success');
                        break;
                    case 'setTime':
                        notify("Time has been updated", 'success');
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
    if ($('#manual-mode-zones option:selected').length === 0) {
        notify("Select minimum one zone!", "danger");
        $('#manual-mode-zones option:first').attr('selected', true);
        return;
    }

    let duration = +$('#duration').val();
    let channels = $('#manual-mode-zones').val().map((val) => +val);

    sendWSCommand("manualIrrigation", { duration, channels });
}

function getSchedule() {
    let $scheduleBlock = $('#schedule-mode');
    let eventSlot = {};
    let checked = $('#schedule-mode-zones').val().map((val) => +val);
    let evId = parseInt($('#evId').val());

    eventSlot.duration = +$scheduleBlock.find('.duration').val();
    eventSlot.channels = checked;
    eventSlot.title = $('.event-title').val();
    // If edit event
    if (!isNaN(evId)) {
        eventSlot.evId = evId;
    }
    let maxDuration = 59;
    switch (parseInt($('#periodicity').val())) {
        case 0:
            eventSlot.minute = +$('.period-block.hourly').find('.time-minute').val();
            eventSlot.second = +$('.period-block.hourly').find('.time-second').val();
            eventSlot.periodicity = 0;

            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 1:
            eventSlot.hours = +$('.period-block.every-x-hours').find('.time-hours').val();
            eventSlot.minute = +$('.period-block.every-x-hours').find('.time-minute').val();
            eventSlot.second = +$('.period-block.every-x-hours').find('.time-second').val();
            eventSlot.periodicity = 1;
            maxDuration = (eventSlot.hours * 60) - 1;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 2:
            var [hour, minute] = $('.period-block.daily').find('.time-hour-minute').val().split(":");
            eventSlot.periodicity = 2;
            eventSlot = { ...eventSlot, ...{ hour, minute } };
            maxDuration = 23 * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 3:
            var [hour, minute] = $('.period-block.every-x-days').find('.time-hour-minute').val().split(":");
            eventSlot.days = +$('.period-block.every-x-days').find('.time-days').val();
            eventSlot.periodicity = 3;
            eventSlot = { ...eventSlot, ...{ hour, minute } };
            maxDuration = ((eventSlot.days * 24) - 1) * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 4:
            var [hour, minute] = $('.period-block.weekly').find('.time-hour-minute').val().split(":");
            eventSlot.dayOfWeek = +$('#weekdays-selector label.active').find('input').val();
            eventSlot.periodicity = 4;
            eventSlot = { ...eventSlot, ...{ hour, minute } };
            maxDuration = ((7 * 24) - 1) * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 5:
            var [hour, minute] = $('.period-block.monthly').find('.time-hour-minute').val().split(":");
            eventSlot.dayOfMonth = +$('.period-block.monthly').find('.time-day-of-month').val();
            eventSlot.periodicity = 5;
            eventSlot = { ...eventSlot, ...{ hour, minute } };
            maxDuration = ((30 * 24) - 1) * 60;
            if (eventSlot.duration <= 0 || eventSlot.duration >= maxDuration) {
                $scheduleBlock.find('.duration').val(maxDuration);
                eventSlot.duration = maxDuration;
            }
            break;
        case 6:
            let date = $('#one-time-datetimepicker').data("DateTimePicker").viewDate();
            eventSlot.year = date.year();
            eventSlot.month = +date.format('MM');
            eventSlot.day = date.date();
            eventSlot.hour = date.hour();
            eventSlot.minute = date.minute();
            eventSlot.second = date.second();
            eventSlot.periodicity = 6;
            break;
    }

    $("#explanation").html(getExplanationForSchedule(eventSlot));

    return eventSlot;
}

function getExplanationForSchedule(scheduleObject) {
    const zonesString = scheduleObject.channels.join(', ');
    const durationStr = moment.duration(scheduleObject.duration, 'minutes').format('HH[h]:mm[m]');
    let title = "";
    let on = "";
    let getExampleText = (currDate, scheduleObject, format = 'YYYY-MM-DD HH:mm:ss') => `<p>Ex: ${currDate.format(format)} - ${moment(currDate).add(scheduleObject.duration, 'm').format(format)}</p>`;
    let getExamples = (date, scheduleObject, c, t) => {
        var explanationString = '';
        for (let i = 0; i < 3; i++) {
            date.add(c, t);
            explanationString += getExampleText(date, scheduleObject);
        }

        return explanationString;
    };
    var currDate = moment();

    switch (scheduleObject.periodicity) {
        case 0:
            currDate.minute(scheduleObject.minute).second(scheduleObject.second);
            on = `${currDate.format('mm:ss')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every hour on ${currDate.format('mm[m]:ss[s]')} with a duration of ${durationStr}\n${getExamples(currDate, scheduleObject, 1, 'h')}`;
            title = "Every hour";
            break;
        case 1:
            currDate.minute(scheduleObject.minute).second(scheduleObject.second);
            on = `${currDate.format('mm:ss')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every ${scheduleObject.hours} hours on ${currDate.format('mm[m]:ss[s]')} with a duration of ${durationStr}\n${getExamples(currDate, scheduleObject, scheduleObject.hours, 'h')}`;
            title = `Every ${scheduleObject.hours} hours`;
            break;
        case 2:
            currDate.hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            on = `${currDate.format('HH:mm')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every day on ${currDate.format('HH[h]:mm[m][00s]')} with a duration of ${durationStr}\n${getExamples(currDate, scheduleObject, 1, 'd')}`;
            title = `Every day`;
            break;
        case 3:
            currDate.hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            on = `${currDate.format('HH:mm')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every ${scheduleObject.days} days on ${currDate.format('HH[h]:mm[m][00s]')} with a duration of ${durationStr}\n${getExamples(currDate, scheduleObject, scheduleObject.days, 'd')}`;
            title = `Every ${scheduleObject.days} days`;
            break;
        case 4:
            var dayOfWeekStr = weekNames[scheduleObject.dayOfWeek - 1];
            currDate.day(scheduleObject.dayOfWeek - 1).hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            on = `${currDate.format('HH:mm')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every ${dayOfWeekStr} on ${currDate.format('HH[h]:mm[m][00s]')} with a duration of ${durationStr}\n${getExamples(currDate, scheduleObject, 1, 'w')}`;
            title = `Every ${dayOfWeekStr}`;
            break;
        case 5:
            currDate.hour(scheduleObject.hour).minute(scheduleObject.minute).second(0);
            currDate.date(scheduleObject.dayOfMonth);
            on = `${currDate.format('HH:mm')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} every month on ${currDate.format('HH[h]:mm[m][00s]')} with a duration of ${durationStr}\n${getExamples(currDate, scheduleObject, 1, 'M')}`;
            title = `Every month`;
            break;
        case 6:
            currDate = moment([scheduleObject.year, scheduleObject.month - 1, scheduleObject.day, scheduleObject.hour, scheduleObject.minute, scheduleObject.second, 0]);
            on = `${currDate.format('YYYY-MM-DD HH:mm:ss')}`;
            explanationString = `Irrigation for zone(s) ${zonesString} on ${currDate.format('YYYY-MM-DD HH:mm:ss')} with a duration of ${durationStr}\n`;
            title = `Once`;
            break;
    }

    $('.event-title').val(`${title} on ${on}`);

    return explanationString;
}

function addOrEditSchedule() {
    let eventSlot = getSchedule();

    if (availableSlots <= 0 && isNaN(eventSlot.evId)) {
        notify("There are no available slots", "danger");
        return;
    }

    if ($('#schedule-mode-zones option:selected').length === 0) {
        notify("Select minimum one zone!", "danger");
        $('#schedule-mode-zones option:selected').attr('selected', true);
        return;
    }

    if (confirm("Are you sure?")) {
        $('.add-schedule').prop("disabled", 1);
        sendWSCommand("addOrEditSchedule", eventSlot);
    }
}

function stopManualIrrigation() {
    sendWSCommand("stopManualIrrigation");
}

function saveWifiConfig() {
    sendWSCommand("saveWiFiConfig", { ssid: $("#ssid").val(), pass: $("#pass").val() });
}

function getWiFiConfig() {
    sendWSCommand("getWiFiConfig");
}

function getSlots() {
    sendWSCommand("getSlots");
}

function removeEvent(evId) {
    sendWSCommand("removeEvent", { evId });
    $('.action-btn').prop('disabled', 1);
}

function setEventEnabled(evId, enabled = true) {
    sendWSCommand(enabled ? "enableEvent" : "disableEvent", { evId, enabled });
    $('.action-btn').prop('disabled', 1);
}

function getSysInfo() {
    sendWSCommand("getSysInfo");
}

function getWaterInfo() {
    sendWSCommand("getWaterInfo");
}

function skipEvent(evId) {
    if (confirm("Are you sure?")) {
        sendWSCommand("skipEvent", { evId });
    }
}

function getChannelNames() {
    sendWSCommand("getChannelNames");
}

function setTime(date) {
    let year = date.year(),
        month = +date.format('MM'),
        day = date.date(),
        hour = date.hour(),
        minute = date.minute(),
        second = date.second();
    currentTime = date;
    sendWSCommand("setTime", { year, month, day, hour, minute, second });
}

function getSettings() {
    sendWSCommand("getSettings");
}

function fetchWeatherForecast() {
    return new Promise(async (resolve, reject) => {
        const values = await Promise.all([getForecastFromAccuWeather(), getForecastFromApixu()]);
        if ($('.forecast-btn').length === 0) {
            $(".fc-right").prepend(`
            <div class="btn-group forecast-btn" data-toggle="buttons">
                <label class="btn btn-sm btn-default forecast-apixu">
                    <input type="radio" checked> Apixu forecast
                </label>
                <label class="btn btn-sm btn-default forecast-accuweather">
                    <input type="radio" checked> AccuWeather forecast
                </label>
            </div>`);
        }
        $('.forecast-apixu').click();
        resolve(values);
    });
}

function getForecastFromAccuWeather() {
    return new Promise((resolve, reject) => {
        let settings = getObjectFromLocalStorage("settings"),
            accuWeatherForecast = getObjectFromLocalStorage("accuWeatherForecast"),
            accuWeatherLastWeatherUpdate = !$.isEmptyObject(accuWeatherForecast) ? accuWeatherForecast.lastWeatherUpdate : moment().unix(),
            accuWeatherLastCityKey = !$.isEmptyObject(accuWeatherForecast) ? accuWeatherForecast.lastCityKey : null;

        // Last update more then 6h ago or location has been changed
        var accuWeatherNeedUpdate = (moment().unix() - accuWeatherLastWeatherUpdate > 21600) || accuWeatherLastCityKey !== settings.accuWeatherCityKey;
        if (settings.accuWeatherCityKey && ($.isEmptyObject(accuWeatherForecast) || accuWeatherNeedUpdate)) {
            $.get(`${accuWeatherApiDomain}/forecasts/v1/daily/5day/${settings.accuWeatherCityKey}?apikey=${accuWeatherAPIKey}&details=true&metric=true&language=ru`)
                .done((weatherData) => {
                    weatherData.lastWeatherUpdate = moment().unix();
                    weatherData.lastCityKey = settings.accuWeatherCityKey;
                    resolve(updateObjectInLocalStorage("accuWeatherForecast", weatherData));
                })
                .fail(reject);
        } else {
            resolve(accuWeatherForecast);
        }
    });
}

function getForecastFromApixu() {
    return new Promise((resolve, reject) => {
        let settings = getObjectFromLocalStorage("settings"),
            apixuForecast = getObjectFromLocalStorage("apixuForecast"),
            apixuLastWeatherUpdate = !$.isEmptyObject(apixuForecast) ? apixuForecast.lastWeatherUpdate : moment().unix(),
            apixuLastWeatherLat = !$.isEmptyObject(apixuForecast) ? apixuForecast.location.lat : null,
            apixuLastWeatherLon = !$.isEmptyObject(apixuForecast) ? apixuForecast.location.lon : null,
            // Last update more then 3h ago or location has been changed
            locationChanged = Math.abs(apixuLastWeatherLat - settings.lat) > 0.2 || Math.abs(apixuLastWeatherLon - settings.lon) > 0.2,
            apixuNeedUpdate = (moment().unix() - apixuLastWeatherUpdate > 10800) || locationChanged;
        if (settings.location && ($.isEmptyObject(apixuForecast) || apixuNeedUpdate)) {
            $.get(`${apixuApiDomain}/forecast.json?key=${apixuAPIKey}&q=${settings.location}&days=10&lang=en`)
                .done((weatherData) => {
                    weatherData.lastWeatherUpdate = moment().unix();
                    resolve(updateObjectInLocalStorage("apixuForecast", weatherData));
                })
                .fail(reject);
        } else {
            resolve(apixuForecast);
        }
    });
}

function showForecastDayOnCalendar(date, iconUrl, iconAlt, tempMin, tempMax, uvIndex, totalprecip, eto = null, Dc, source) {
    let monthDayElement = $(`td.fc-day[data-date="${date}"]`),
        weekHeaderElement = $(`th.fc-day-header[data-date="${date}"]`),
        uvIndexColor = "green",
        uvIndexTitle = "No protection required";
    if (uvIndex >= 3 && uvIndex <= 5) {
        uvIndexColor = "#f8af44";
        uvIndexTitle = "Protection required";
    } else if (uvIndex > 5 && uvIndex <= 7) {
        uvIndexColor = "#f7941e";
        uvIndexTitle = "Protection essential";
    } else if (uvIndex > 7 && uvIndex <= 10) {
        uvIndexColor = "#de401f";
        uvIndexTitle = "Need shade";
    } else if (uvIndex > 11) {
        uvIndexColor = "#aa5b99";
        uvIndexTitle = "Can't go outdor";
    }

    //TODO: Change Dc color depending on the Dc value
    let DcColor = '#333';

    let template = `
    <div class='weather-block'>
        <img class="${source}" src='${iconUrl}' alt='${iconAlt}' title='${iconAlt}'/>
        <div class="hidden-xs">
            <span title="Min temperature" style="font-size: 11px"><i class="fa fa-temperature-low"/> ${tempMin}°C</span>                    
            <span title="Max temperature" style="font-size: 11px"><i class="fa fa-temperature-high"/> ${tempMax}°C</span>        
        </div>
        <div class="hidden-xs">
            <span title="UV-index(${uvIndexTitle})" style="font-size: 11px; color: ${uvIndexColor}"><i class="fa fa-sun"/> ${uvIndex}</span>
        </div>
        <div class="hidden-xs" style="margin: -5px 0 0 5px">
            ${!eto ? "" : `<span title="Evapotranspiration" style="font-size: 11px"><b>ETo:</b> ${eto}mm</span>`}
            <span title="Amount of precipitation" style="font-size: 11px"><i class="fa fa-tint"/> ${totalprecip}mm </span>
        </div>
        <!--
        TODO: Complete calculation Dc and show this bloc
        <div class="hidden-xs" style="margin-left: 5px">
            <span title="Soil moisture deficit" style="font-size: 11px"><b>Dc</b>: ${Dc}mm</span>
        </div>-->
    </div>`;

    monthDayElement.append(template);
    weekHeaderElement.append(template);
}

function showForecastFromApixuOnCalendar() {
    Promise
        .all([getForecastFromAccuWeather(), getForecastFromApixu()])
        .then((weatherData) => {
            let [accuWeatherForecast, apixuForecast] = weatherData;
            let settings = getObjectFromLocalStorage("settings");
            if (!$.isEmptyObject(apixuForecast)) {
                $('.weather-block').remove();
                let Dp = 0;
                let Dc = 0;
                $.each(apixuForecast.forecast.forecastday, (index, forecast) => {
                    let hoursOfSun = null;

                    $.each(accuWeatherForecast.DailyForecasts, (index, accuWeatherForecastDay) => {
                        if (moment.unix(accuWeatherForecastDay.EpochDate).format("YYYY-MM-DD") === moment(forecast.date).format("YYYY-MM-DD")) {
                            hoursOfSun = +accuWeatherForecastDay.HoursOfSun;
                            return false;
                        }
                    });

                    let eto = ETo(
                        +forecast.day.maxtemp_c,
                        +forecast.day.mintemp_c,
                        null,
                        null,
                        +forecast.day.avghumidity / 100,
                        hoursOfSun,
                        null,
                        moment().date(),
                        moment().month() - 1,
                        settings.lat,
                        settings.elevation,
                        +forecast.day.maxwind_kph
                    );

                    Dc = Dp + +eto - +forecast.day.totalprecip_mm;
                    Dc = Dc.toFixed(1);
                    Dp = +Dc;

                    showForecastDayOnCalendar(
                        forecast.date,
                        forecast.day.condition.icon,
                        forecast.day.condition.text,
                        forecast.day.mintemp_c,
                        forecast.day.maxtemp_c,
                        forecast.day.uv,
                        forecast.day.totalprecip_mm,
                        eto,
                        Dc,
                        'apixu'
                    );
                });
            }
        });
}

function showForecastFromAccuWeatherOnCalendar() {
    getForecastFromAccuWeather().then((weatherData) => {
        if (!$.isEmptyObject(weatherData)) {
            $('.weather-block').remove();
            let settings = getObjectFromLocalStorage("settings");
            let Dp = 0;
            let Dc = 0;
            $.each(weatherData.DailyForecasts, (index, forecast) => {
                let date = moment(moment.unix(forecast.EpochDate)).format("YYYY-MM-DD"),
                    dayIcon = `https://developer.accuweather.com/sites/default/files/${forecast.Day.Icon.toString().padStart(2, 0)}-s.png`,
                    totalprecip = (+forecast.Day.TotalLiquid.Value + +forecast.Night.TotalLiquid.Value).toFixed(1),
                    uvIndex = -1;
                $.each(forecast.AirAndPollen, (index, el) => {
                    if (el.Name === "UVIndex") {
                        uvIndex = el.Value;
                        return false;
                    }
                });

                let eto = ETo(
                    +forecast.Temperature.Maximum.Value,
                    +forecast.Temperature.Minimum.Value,
                    null,
                    null,
                    null,
                    +forecast.HoursOfSun,
                    null,
                    moment().date(),
                    moment().month() - 1,
                    settings.lat,
                    settings.elevation,
                    +forecast.Day.Wind.Speed.Value
                );
                Dc = Dp + +eto - +totalprecip;
                Dc = Dc.toFixed(1);
                Dp = +Dc;

                showForecastDayOnCalendar(
                    date,
                    dayIcon,
                    forecast.Day.LongPhrase,
                    forecast.Temperature.Minimum.Value,
                    forecast.Temperature.Maximum.Value,
                    uvIndex,
                    totalprecip,
                    eto,
                    Dc,
                    'accuweather'
                );
            });
        }
    });
}

function getObjectFromLocalStorage(name) {
    return JSON.parse(localStorage.getItem(name)) || {};
}

function updateObjectInLocalStorage(name, options) {
    let object = { ...getObjectFromLocalStorage(name), ...options };
    localStorage.setItem(name, JSON.stringify(object));

    return object;
}

function ETo(Tmax, Tmin, RHmin, RHmax, RHmean, hoursOfSun, pressure, day, month, lat, alt, windSpeed) {
    //Input parameters
    let n = hoursOfSun, //Hours of sun from weather forecast
        P = pressure, //Atmospheric pressure from weather station. kPa
        Y = moment().year(),
        D = day, //Current day
        M = month, //Current month
        bissextile = (Y % 4 != 0 || Y % 100 == 0 && Y % 400 != 0) ? false : true,
        latitude = lat, // Device latitude. Get from settings
        altitude = alt, //Altitude from sealevel. meters
        WSkmhOn10m = windSpeed, //Wind speed from weather station. km/h
        //Wind speed transformation   
        WSmsOn10m = WSkmhOn10m / 3.6,
        u2 = 0.748 * WSmsOn10m; // Wind speed on height 2m in m/s

    //Parameters
    if (!P && alt) {
        P = 101.3 * (((293 - 0.0065 * alt) / 293)) ** 5.26;
    }

    let Tmean = (Tmax + Tmin) / 2,
        delta = (4098 * (0.6108 * Math.exp((17.27 * Tmean) / (Tmean + 237.3)))) / (Tmean + 237.3) ** 2,
        gamma = 0.000665 * P;

    //Steam pressure deficiency
    let e0_Tmax = 0.6108 * Math.exp((17.27 * Tmax) / (Tmax + 237.3)),
        e0_Tmin = 0.6108 * Math.exp((17.27 * Tmin) / (Tmin + 237.3)),
        es = (e0_Tmax + e0_Tmin) / 2;

    let ea = 0;
    if (RHmin && RHmax) {
        ea = ((e0_Tmin * RHmax) + (e0_Tmax * RHmin)) / 2;
    } else if (RHmean) {
        ea = RHmean * ((e0_Tmax + e0_Tmin) / 2);
    } else if (!RHmin && !RHmax && !RHmean) {
        ea = e0_Tmin;
    }

    //Radiation    
    let J = ~~(275 * M / 9 - 30 + D) - 2;

    if (M < 3) {
        J += 2;
    }

    if (bissextile && M > 2) {
        J += 1;
    }

    let dr = 1 + 0.033 * Math.cos(((2 * Math.PI) / 365) * J),
        delta_sol = 0.4098 * Math.sin((((2 * Math.PI) / 365) * J) - 1.39),
        fi = (Math.PI / 180) * latitude,
        omega_s = Math.acos((Math.tan(fi) * -1) * Math.tan(delta_sol)),
        //Extraterrestrial radiation for the day period
        Ra = ((24 * 60) / Math.PI) * 0.0820 * dr * (omega_s * (Math.sin(fi) * Math.sin(delta_sol)) + (Math.cos(fi) * Math.cos(delta_sol)) * Math.sin(omega_s)),
        //Daylight hours
        N = (24 / Math.PI) * omega_s,
        //Relative sunshine duration
        nN = n / N,
        //Sun radiation    
        Rs = (0.25 + 0.5 * nN) * Ra,
        //Sun radiation in clear sky
        Rso = (0.75 + 2e-5 * altitude) * Ra,
        //Pure shortwave radiation
        Rns = (1 - 0.23) * Rs,
        //Pure longwave radiation
        sigmaTmax_k4 = 4.903e-9 * (Tmax + 273.16) ** 4,
        sigmaTmin_k4 = 4.903e-9 * (Tmin + 273.16) ** 4,
        Rnl = ((sigmaTmax_k4 + sigmaTmin_k4) / 2) * (0.34 - 0.14 * Math.sqrt(ea)) * (1.35 * (Rs / Rso) - 0.35),
        //Pure longwave radiation
        Rn = Rns - Rnl,
        //Soil heat flux
        G = 0, //Ignore for 24-hour period
        //Etalon evapotranspiration
        ETo = ((0.408 * (Rn - G)) * delta / (delta + gamma * (1 + 0.34 * u2))) + (900 / (Tmean + 273) * (es - ea) * gamma / delta + gamma * (1 + 0.34 * u2));

    return (+ETo).toFixed(1);
}