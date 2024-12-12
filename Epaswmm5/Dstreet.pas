unit Dstreet;

{-------------------------------------------------------------------}
{                    Unit:    Dstreet.pas                           }
{                    Project: EPA SWMM                              }
{                    Version: 5.3                                   }
{                    Date:    10/31/24   (5.3.0)                    }
{                    Author:  L. Rossman                            }
{                                                                   }
{   Dialog form used to specify the properties of a Street cross    }
{   section.                                                        }
{-------------------------------------------------------------------}

interface

uses
  Winapi.Windows, Winapi.Messages, System.SysUtils, System.Variants,
  System.Classes, Vcl.Graphics,  Vcl.Controls, Vcl.Forms, Vcl.Dialogs,
  Vcl.StdCtrls, NumEdit, Vcl.ExtCtrls, System.ImageList, Vcl.ImgList,
  Vcl.VirtualImageList, Vcl.BaseImageCollection, Vcl.ImageCollection,
  Uutils, Uproject, Uglobals;

type
  TStreetEditorForm = class(TForm)
    Panel1: TPanel;
    ImageStreetXSection: TImage;
    Label5: TLabel;
    NumEditTCrown: TNumEdit;
    Label2: TLabel;
    NumEditHCurb: TNumEdit;
    Label3: TLabel;
    NumEditSx: TNumEdit;
    Label6: TLabel;
    NumEditRRoughness: TNumEdit;
    NumEditGutterWidth: TNumEdit;
    Label4: TLabel;
    NumEditGutterDepression: TNumEdit;
    Label7: TLabel;
    NumEditBackingWidth: TNumEdit;
    Label1: TLabel;
    NumEditBackingSlope: TNumEdit;
    Label8: TLabel;
    NumEditBackingRoughness: TNumEdit;
    Label9: TLabel;
    RadioButtonTwoSided: TRadioButton;
    RadioButtonOneSided: TRadioButton;
    Label10: TLabel;
    Label11: TLabel;
    ButtonHelp: TButton;
    ButtonCancel: TButton;
    ButtonOK: TButton;
    UnitLabel1: TLabel;
    UnitLabel2: TLabel;
    Label12: TLabel;
    UnitLabel4: TLabel;
    UnitLabel3: TLabel;
    Label13: TLabel;
    UnitLabel5: TLabel;
    NameEdit: TNumEdit;
    Label15: TLabel;
    ImageCollectionStreetXSection: TImageCollection;
    VirtualImageListStreetXSection: TVirtualImageList;
    procedure FormCreate(Sender: TObject);
    procedure ButtonOKClick(Sender: TObject);
    procedure ButtonHelpClick(Sender: TObject);
    procedure FormKeyDown(Sender: TObject; var Key: Word; Shift: TShiftState);
    procedure NameEditChange(Sender: TObject);
    procedure NameEditKeyPress(Sender: TObject; var Key: Char);
  private
    { Private declarations }
    StreetIndex: Integer;
    RadioButtonOneSidedChecked: Boolean;
  public
    { Public declarations }
    Modified: Boolean;
    procedure SetData(const I: Integer; const S: String; aStreet: TStreet);
    procedure GetData(var S: String; aStreet: TStreet);
  end;

//var
//  StreetEditorForm: TStreetEditorForm;

implementation

{$R *.dfm}

procedure TStreetEditorForm.ButtonOKClick(Sender: TObject);
var
  I: Integer;
  X: Single;
  S: String;
  C: TNumEdit;
begin
  S := Trim(NameEdit.Text);
  if (Length(S) = 0) then
  begin
    Uutils.MsgDlg('Name field cannot be left blank.', mtError, [mbOK]);
    NameEdit.SetFocus;
    Exit;
  end;
  I := Project.Lists[STREET].IndexOf(S);
  if (I >= 0) and (I <> StreetIndex) then
  begin
    Uutils.MsgDlg('A Street Section with this name already exists.',
      mtError, [mbOK]);
    NameEdit.SetFocus;
    Exit;
  end;
  for I := 0 to 3 do
  case I of
    STREET_CROWN_WIDTH: C := NumEditTCrown;
    STREET_CURB_HEIGHT: C := NumEditHCurb;
    STREET_CROSS_SLOPE: C := NumEditSx;
    STREET_ROUGHNESS: C := NumEditGutterDepression;
  begin

    //   begin
    //     with C as TNumEdit do
    //     begin
    //       S := Trim(Text);
    //       if Length(S) = 0 then 
    //       begin
    //         Uutils.MsgDlg('Required field cannot be left blank.', mtError, [mbOK]);
    //         SetFocus;
    //         Exit;
    //       end;
    //       Uutils.GetSingle(S, X);
    //       if X = 0 then
    //       begin
    //         Uutils.MsgDlg('Required value cannot be 0.', mtError, [mbOK]);
    //         SetFocus;
    //         Exit;
    //       end;
    //     end;
    //   end;
    // end;
  end;
  ModalResult := mrOK;
end;

procedure TStreetEditorForm.FormCreate(Sender: TObject);
var
  LengthUnits: String;
begin
  LengthUnits := 'ft';
  if Uglobals.UnitSystem = usSI then LengthUnits := 'm';
  UnitLabel1.Caption := LengthUnits;
  UnitLabel2.Caption := LengthUnits;
  UnitLabel3.Caption := LengthUnits;
  UnitLabel4.Caption := LengthUnits;
  UnitLabel5.Caption := LengthUnits;
  VirtualImageListStreetXSection.GetIcon(0, ImageStreetXSection.Picture.Icon);
end;

procedure TStreetEditorForm.SetData(const I: Integer; const S: String;
  aStreet: TStreet);
var
  J: Integer;
begin
  StreetIndex := I;
  NameEdit.Text := S;

  case J of
    STREET_CROWN_WIDTH: NumEditTCrown.Text := aStreet.Data[STREET_CROWN_WIDTH];
    STREET_CURB_HEIGHT: NumEditHCurb.Text := aStreet.Data[STREET_CURB_HEIGHT];
    STREET_CROSS_SLOPE: NumEditSx.Text := aStreet.Data[STREET_CROSS_SLOPE];
    STREET_ROUGHNESS: NumEditRRoughness.Text := aStreet.Data[STREET_ROUGHNESS];
    STREET_DEPRESSION: NumEditGutterDepression.Text := aStreet.Data[STREET_DEPRESSION];
    STREET_GUTTER_WIDTH: NumEditGutterWidth.Text := aStreet.Data[STREET_GUTTER_WIDTH];
    STREET_BACK_WIDTH: NumEditBackingWidth.Text := aStreet.Data[STREET_BACK_WIDTH];
    STREET_BACK_SLOPE: NumEditBackingSlope.Text := aStreet.Data[STREET_BACK_SLOPE];
    STREET_BACK_ROUGHNESS: NumEditBackingRoughness.Text := aStreet.Data[STREET_BACK_ROUGHNESS];
    STREET_SIDES:
    begin
      if SameText(aStreet.Data[STREET_SIDES], '1') then
        RadioButtonOneSided.Checked := True;
        
    end;
  end;
  RadioButtonOneSidedChecked := RadioButtonOneSided.Checked;
  Modified := false;
end;

procedure TStreetEditorForm.GetData(var S: String; aStreet: TStreet);
var
  J: Integer;
begin
  S := NameEdit.Text;

  if RadioButtonOneSided.Checked then
    aStreet.DATA[STREET_SIDES] := '1'
  else
    aStreet.DATA[STREET_SIDES] := '2';
  
  aStreet.Data[STREET_CROWN_WIDTH] := NumEditTCrown.Text;

  for J := 0 to MAXSTREETPROPS do
  begin
    if J = STREET_SIDES then
    begin
      if RadioButtonOneSided.Checked then
        aStreet.DATA[STREET_SIDES] := '1'
      else
        aStreet.DATA[STREET_SIDES] := '2';
    end
    else with FindComponent('NumEdit' + IntToStr(J)) as TNumEdit do
    begin
      if Length(Trim(Text)) > 0 then aStreet.Data[J] := Trim(Text)
      else aStreet.Data[J] := '0';
    end;
  end;
  if RadioButtonOneSidedChecked <> RadioButtonOneSided.Checked then Modified := true;
  aStreet.SetMaxDepth;
end;

procedure TStreetEditorForm.NameEditChange(Sender: TObject);
begin
  Modified := true;
end;

procedure TStreetEditorForm.NameEditKeyPress(Sender: TObject; var Key: Char);
begin
  with NameEdit as TNumEdit do
  begin
    if (Length(Text) = 0) or (SelStart = 0) then
      if Key = '[' then Key := #0;
  end;
end;

procedure TStreetEditorForm.FormKeyDown(Sender: TObject; var Key: Word;
  Shift: TShiftState);
begin
  if Key = VK_F1 then ButtonOKClick(Sender);
end;

procedure TStreetEditorForm.ButtonHelpClick(Sender: TObject);
begin
  Application.HelpCommand(HELP_CONTEXT, 213580);
end;

end.
