

//static bool getLocalTime(struct tm * info, uint32_t);


//  Splits a string by the separator character
/*
Examples:
  Serial.println(splitString("pippo:pluto:paperino", ':', 0));
  Serial.println(splitString("pippo:pluto:paperino", ':', 1));
  Serial.println(splitString("pippo:pluto:paperino", ':', 2));
*/


String splitString(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {
    0, -1  };
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
      found++;
      strIndex[0] = strIndex[1]+1;
      strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }
  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}