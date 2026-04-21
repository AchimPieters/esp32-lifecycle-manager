import { CropMarks } from '../../../src/core/marks/CropMarks';
import { RegistrationMarks } from '../../../src/core/marks/RegistrationMarks';
import { Bleed } from '../../../src/core/marks/Bleed';
import { PDFValidationError } from '../../../src/utils/errors';

describe('marks modules', () => {
  it('generates crop marks', () => {
    const crop = new CropMarks();
    const marks = crop.generate(100, 200);

    expect(marks).toHaveLength(8);
    expect(() => crop.generate(0, 200)).toThrow(PDFValidationError);
  });

  it('generates registration marks', () => {
    const registration = new RegistrationMarks();
    const marks = registration.generate(100, 200);

    expect(marks).toHaveLength(4);
    expect(() => registration.generate(-1, 10)).toThrow(PDFValidationError);
  });

  it('calculates bleed box', () => {
    const bleed = new Bleed();
    const box = bleed.apply(100, 200, 3);

    expect(box).toEqual({ x: -3, y: -3, width: 106, height: 206 });
    expect(() => bleed.apply(100, 100, -1)).toThrow(PDFValidationError);
  });
});
