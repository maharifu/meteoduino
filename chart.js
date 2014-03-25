/*
 * Compile with uglify.js
 * uglifyjs chart.js -mt -m sort -e | tr \" \' | sed "s/\\\n/\\\\\\\n/" | sed "s/^[^{]\+{\(.*\)})();$/<script>\1<\/script>/"
 * remove the surrounding function() and escape \n
 */
var values = document.querySelector("#x").innerHTML.split("\n"),
    humi = [],
    temp = [];

for(i=0;i<values.length;i++) {
  var v = values[i].split(" ");
  if( v.length == 4 || v.length == 2) {
    if( v.length == 4) {
      var cur_time = parseInt(v[0]),
          interval = parseInt(v[1]),
          cur_humi = parseInt(v[2]),
          cur_temp = parseInt(v[3]);
    } else {
      cur_time += interval;
      cur_humi += parseInt(v[0]);
      cur_temp += parseInt(v[1]);
    }
    humi.push([cur_time * 1000, cur_humi / 10.0]);
    temp.push([cur_time * 1000, cur_temp / 10.0]);
  }
}

var chart = new Highcharts.Chart({
  chart: {
    renderTo: 'x',
    zoomType: 'x'
  },
  title: {
    text: 'Meteo'
  },
  xAxis: {
    type: 'datetime',
    maxZoom: 3600
  },
  yAxis: {
    title: {
      text: 'Meteo'
    }
  },
  series: [{
    name: 'Humidity',
    data: humi
  }, {
    name: 'Temperature',
    data: temp
  }]
});

var source = new EventSource('/i');
source.onmessage = function(evt) {
  var data = evt.data.split(" ");
  chart.series[0].addPoint([parseInt(data[0]) * 1000,parseInt(data[1]) / 10.0]);
  chart.series[1].addPoint([parseInt(data[0]) * 1000,parseInt(data[2]) / 10.0]);
};
